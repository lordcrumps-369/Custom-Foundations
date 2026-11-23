#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include "foundation_policy.hpp"

#include "compatible_image.hpp"

namespace {
uintptr_t game_base;
std::atomic<bool> initialized{false};
thread_local int calculation_id=-1;
thread_local brick::Selection selection;
thread_local brick::Context context;
using brick::Coordinate;
using brick::Point;
using brick::Segment;
template<class F> F native(uintptr_t va) {
#ifdef BRICK_TESTING
    return reinterpret_cast<F>(resolve_test_native(va));
#else
    return reinterpret_cast<F>(game_base+va-0x400000);
#endif
}
template<class F> F method(void* self,unsigned slot) { return reinterpret_cast<F>((*static_cast<uintptr_t**>(self))[slot/4]); }
struct World {
    void* self;
    bool vertex_valid(const Coordinate& c) { return method<bool(__thiscall*)(void*,const Coordinate*)>(self,0x118)(self,&c); }
    bool tile_valid(const Coordinate& c) { return method<bool(__thiscall*)(void*,const Coordinate*)>(self,0x11c)(self,&c); }
    float height(const Coordinate& c) { return method<float(__thiscall*)(void*,const Coordinate*)>(self,0xf8)(self,&c); }
    bool floor(const Coordinate& c) { return method<int(__thiscall*)(void*,const Coordinate*)>(self,0xe8)(self,&c)!=-1; }
    int bound(unsigned slot) { return method<int(__thiscall*)(void*)>(self,slot)(self); }
    int min_x() { return bound(0x144); } int min_y() { return bound(0x148); }
    int max_x() { return bound(0x14c); } int max_y() { return bound(0x150); }
    bool brick_wall(const Segment& edge) {
        Coordinate a{int(edge.a.x),int(edge.a.y),0}, b{int(edge.b.x),int(edge.b.y),0};
        if(!vertex_valid(a)||!vertex_valid(b)) return false;
        void* walls=method<void*(__thiscall*)(void*)>(self,0x9c)(self);
        if(!walls) return false;
        // These are the same existence/style queries used at C278FD/C2790C.
        if(!method<bool(__thiscall*)(void*,const Segment*)>(walls,0xec)(walls,&edge)) return false;
        return method<int(__thiscall*)(void*,const Segment*)>(walls,0x104)(walls,&edge)==23;
    }
};

struct IdScope { int previous; explicit IdScope(int id):previous(calculation_id){calculation_id=id;} ~IdScope(){calculation_id=previous;} };
struct CommitScope { brick::Context previous; CommitScope(int id,const Coordinate* a,const Coordinate* b):previous(context){ context={}; if(brick::steps(id)&&a&&b)context={id,{a->x,a->y,a->z},{b->x,b->y,b->z}}; } ~CommitScope(){context=previous;} };

float __cdecl calculate(const Point* point,int category,const void* tool) {
    int id=-1;
    int raw_id=-1,declared_id=-1;
    if(tool && category==2) {
        const auto bytes=static_cast<const BYTE*>(tool);
        raw_id=*reinterpret_cast<const int*>(bytes+0x74);
        declared_id=*reinterpret_cast<const int*>(bytes+0x78);
        if(brick::steps(declared_id)) id=declared_id;
    }
    // Observed live in v0.2: runtime 15/17/19 and declaration 70/71/72.
    // Do not assume an arithmetic relationship between those fields.
    selection={raw_id,id};
    IdScope scope(brick::steps(id)?id:-1);
    const float result=native<float(__cdecl*)(const Point*,int)>(0x609725)(point,category);
    selection={raw_id,id};
    return result;
}
float __fastcall placement_step(void* world,void*) {
    const float original=method<float(__thiscall*)(void*)>(world,0x178)(world);
    const int requested=brick::steps(calculation_id);
    const float result=requested ? original*float(requested)/4.0f : original;
    return result;
}
bool __cdecl commit(int id,const Coordinate* a,const Coordinate* b,float height,int flags,void* commands) {
    const int script_id=selection.for_commit(id);
    CommitScope scope(script_id,a,b);
    return native<bool(__cdecl*)(int,const Coordinate*,const Coordinate*,float,int,void*)>(0x60bfe9)(id,a,b,height,flags,commands);
}
// Receives the actual native product's diagonal flag; no runtime-ID arithmetic.
void __cdecl build_plane(int category,int diagonal,const Coordinate* a,const Coordinate* b,int apply,float height,void* commands) {
    if(context.id==70 && a && b && apply && a->z==0 && b->z==0)
        context.diagonal=diagonal!=0;
    native<void(__cdecl*)(int,int,const Coordinate*,const Coordinate*,int,float,void*)>(0x60aab1)(category,diagonal,a,b,apply,height,commands);
}
float __fastcall build_step(void* world,void*) {
    const float original=method<float(__thiscall*)(void*)>(world,0x178)(world);
    // Only the new short foundation needs a smaller separation here. The
    // caller still validates the vertex and performs native transactions.
    return context.id==70 && context.first.z==0 && context.last.z==0 ? original*0.5f : original;
}
void __cdecl adjust_lower_gap(void* self,const Coordinate* coordinate,float* gap) {
    const DWORD error=GetLastError();
    try {
        if(self&&coordinate&&gap) {
            World world{self};
            const float before=*gap;
            *gap=brick::lower_gap(world,*coordinate,before,context);
        }
    } catch(...) { /* Retain the native gap if inspection is unavailable. */ }
    SetLastError(error);
}

}

extern "C" __declspec(naked) void calculation_adapter() {
    __asm {
        push ebx
        push dword ptr [esp+12]
        push dword ptr [esp+12]
        call calculate
        add esp,12
        ret
    }
}
uintptr_t lower_return;
extern "C" __declspec(naked) void lower_adapter() {
    __asm {
        pushfd
        pushad
        sub esp,544
        lea eax,[esp+15]
        and eax,0xfffffff0
        mov dword ptr [esp+528],eax
        fxsave [eax]
        fninit
        lea eax,[ebp-8]
        push eax
        push edi
        push esi
        call adjust_lower_gap
        add esp,12
        mov eax,dword ptr [esp+528]
        fxrstor [eax]
        add esp,544
        popad
        popfd
        mov eax,dword ptr [edi]
        fsub dword ptr [ebp-8]
        jmp dword ptr [lower_return]
    }
}

namespace {
struct Patch { uintptr_t va; BYTE expected[6]; unsigned size; void* target; bool jump; };
bool install() {
    Patch patches[]={
        {0x60c0a1,{0xe8,0x0b,0xea,0xff,0xff},5,reinterpret_cast<void*>(&build_plane),false},
        {0x4d6dcb,{0xe8,0x55,0x29,0x13,0x00},5,reinterpret_cast<void*>(&calculation_adapter),false},
        {0x60982b,{0xff,0x90,0x78,0x01,0x00,0x00},6,reinterpret_cast<void*>(&placement_step),false},
        {0x4d7856,{0xe8,0x8e,0x47,0x13,0x00},5,reinterpret_cast<void*>(&commit),false},
        {0x609883,{0xff,0x90,0x78,0x01,0x00,0x00},6,reinterpret_cast<void*>(&build_step),false},
        {0x4b79d3,{0x8b,0x07,0xd8,0x65,0xf8},5,reinterpret_cast<void*>(&lower_adapter),true}
    };
    constexpr unsigned count=sizeof(patches)/sizeof(patches[0]);
    BYTE* locations[count]{}; DWORD protections[count]{};
    for(unsigned i=0;i<count;++i) {
        locations[i]=reinterpret_cast<BYTE*>(game_base+patches[i].va-0x400000);
        BYTE actual[6]{}; SIZE_T bytes_read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),locations[i],actual,patches[i].size,&bytes_read)||bytes_read!=patches[i].size||memcmp(actual,patches[i].expected,bytes_read)) {
            return false;
        }
    }
    for(unsigned i=0;i<count;++i) {
        if(!VirtualProtect(locations[i],patches[i].size,PAGE_EXECUTE_READWRITE,&protections[i])) {
            while(i){--i;DWORD ignored;VirtualProtect(locations[i],patches[i].size,protections[i],&ignored);}
            return false;
        }
    }
    lower_return=game_base+0x4b79d8-0x400000;
    for(unsigned i=0;i<count;++i) {
        BYTE replacement[6]={BYTE(patches[i].jump?0xe9:0xe8),0,0,0,0,0x90};
        const int32_t relative=int32_t(reinterpret_cast<uintptr_t>(patches[i].target)-reinterpret_cast<uintptr_t>(locations[i]+5));
        memcpy(replacement+1,&relative,4);memcpy(locations[i],replacement,patches[i].size);
    }
    for(unsigned i=count;i;--i) { DWORD ignored; FlushInstructionCache(GetCurrentProcess(),locations[i-1],patches[i-1].size); VirtualProtect(locations[i-1],patches[i-1].size,protections[i-1],&ignored); }
    return true;
}
}
extern "C" __declspec(dllexport) void InitializeASI() {
    if(initialized.exchange(true))return;
    game_base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(compatibility::accepts(game_base))install();
}
static DWORD WINAPI initialize_from_loader(void*) { InitializeASI(); return 0; }
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,initialize_from_loader,nullptr,0,nullptr);
        if(thread)CloseHandle(thread);
    }
    return TRUE;
}
