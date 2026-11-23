#define NOMINMAX
#include <cassert>
#include <cstdio>
#include <limits>
#include <map>
#include <tuple>
#include <windows.h>
#include "foundation_policy.hpp"
void* resolve_test_native(uintptr_t);
#define BRICK_TESTING
#include "plugin.cpp"

struct Fixture {
    int wall=23;
    bool cap=true;
    float rise=0.375f;
    int min_x(){return 0;} int min_y(){return 0;}
    int max_x(){return 5;} int max_y(){return 5;}
    bool vertex_valid(const Coordinate& c){return c.x>=0&&c.x<=6&&c.y>=0&&c.y<=6&&c.z>=0&&c.z<=2;}
    bool tile_valid(const Coordinate& c){return c.x>=0&&c.x<=5&&c.y>=0&&c.y<=5&&c.z>=0&&c.z<=2;}
    bool floor(const Coordinate& c){assert(tile_valid(c));return cap&&c.z==1&&c.x>=1&&c.x<4&&c.y>=1&&c.y<4;}
    float height(const Coordinate& c){assert(vertex_valid(c));return c.z==0?0.0f:c.z==1?rise:3.375f;}
    bool brick_wall(const Segment& e){return wall==23&&e.a.x==1&&e.b.x==1;}
};
static int plane_calls=0;
static void __cdecl mock_plane(int category,int diagonal,const Coordinate* a,const Coordinate* b,int apply,float height,void* commands){
    assert(category==2 && diagonal==1 && apply==1 && height==0.375f && commands==(void*)0xabc);
    assert(a->x==5 && b->x==9 && context.diagonal && context.contains({7,17,1}));++plane_calls;
}
static const Point* observed_point;
static int observed_category, observed_id;
float __cdecl mock_calculation(const Point* p,int category) {
    observed_point=p;observed_category=category;observed_id=calculation_id;
    SetLastError(771);return 12.5f;
}
bool __cdecl mock_commit(int id,const Coordinate* a,const Coordinate* b,float height,int flags,void* commands) {
    assert(id==15&&a->x==1&&b->x==4&&height==0.375f&&flags==123&&commands==reinterpret_cast<void*>(0xabc));
    assert(context.id==70&&context.first.x==1&&context.last.x==4);
    SetLastError(991);return true;
}
void* resolve_test_native(uintptr_t va) {
    if(va==0x60aab1)return reinterpret_cast<void*>(&mock_plane);
    if(va==0x609725)return reinterpret_cast<void*>(&mock_calculation);
    if(va==0x60bfe9)return reinterpret_cast<void*>(&mock_commit);
    assert(false);return nullptr;
}

__declspec(naked) float call_calculation(const Point*,int,void*) {
    __asm {
        push ebx
        mov ebx,dword ptr [esp+16]
        push dword ptr [esp+12]
        push dword ptr [esp+12]
        call calculation_adapter
        add esp,8
        pop ebx
        ret
    }
}
static unsigned before_flags,after_flags;
static float second_value;
static unsigned before_xmm[4]={0x3f800000,0x40000000,0x40400000,0x40800000};
static unsigned after_xmm[4];
float __fastcall fake_step(void*,void*) { SetLastError(1357);return brick::step; }

bool __fastcall no_coordinate(void*,void*,const Coordinate*) {return false;}
__declspec(naked) float call_lower(Coordinate*,float,float) {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,16
        push ebx
        push esi
        push edi
        mov edi,dword ptr [ebp+8]
        mov esi,1
        mov eax,dword ptr [ebp+16]
        mov dword ptr [ebp-8],eax
        movups xmm0,xmmword ptr [before_xmm]
        fld1
        fld dword ptr [ebp+12]
        mov eax,offset resumed
        mov dword ptr [lower_return],eax
        cmp esi,1
        pushfd
        pop dword ptr [before_flags]
        jmp lower_adapter
    resumed:
        pushfd
        pop dword ptr [after_flags]
        movups xmmword ptr [after_xmm],xmm0
        fstp dword ptr [ebp-12]
        fstp dword ptr [second_value]
        fld dword ptr [ebp-12]
        pop edi
        pop esi
        pop ebx
        mov esp,ebp
        pop ebp
        ret
    }
}
#include "diagonal-native-tests.hpp"
int main() {
    test_native_diagonal_bounds();
    // Diamond opposite endpoints omit its projecting north/south corners.
    brick::Context diamond{70,{5,19,0},{9,19,0}};
    assert(!diamond.contains({7,17,1}));diamond.diagonal=true;
    for(int x=4;x<=10;++x)for(int y=16;y<=22;++y)assert(diamond.contains({x,y,1}));
    assert(!diamond.contains({7,15,1})&&!diamond.contains({11,19,1})&&!diamond.contains({7,17,2}));
    for(int rotation=0;rotation<4;++rotation)for(int reverse=0;reverse<2;++reverse){
        brick::Coordinate a{5,19,0},b{9,19,0},tip{7,17,1};
        for(int r=0;r<rotation;++r){int ax=a.x, bx=b.x,tx=tip.x;a.x=-a.y;a.y=ax;b.x=-b.y;b.y=bx;tip.x=-tip.y;tip.y=tx;}
        if(reverse)std::swap(a,b);
        brick::Context d{70,a,b,true};assert(d.contains(tip));
    }
    context={70,{5,19,0},{9,19,0}};brick::Coordinate pa{5,19,0},pb{9,19,0};
    build_plane(2,1,&pa,&pb,1,0.375f,(void*)0xabc);assert(plane_calls==1&&context.diagonal);
    context={};
    Fixture world;
    assert(brick::steps(70)==2&&brick::steps(71)==6&&brick::steps(72)==8&&brick::steps(0)==0);
    brick::Selection observed{15,70};
    assert(observed.for_commit(15)==70&&observed.for_commit(17)==-1);
    brick::Context inactive;
    Coordinate c{3,3,1};
    assert(brick::short_brick_floor(world,c));
    assert(brick::lower_gap(world,c,0.75f,inactive)==0.1875f);
    world.wall=16;assert(!brick::short_brick_floor(world,c));
    assert(brick::lower_gap(world,c,0.75f,inactive)==0.75f);
    world.wall=23;world.cap=false;assert(!brick::short_brick_floor(world,c));
    world.cap=true;world.rise=0.75f;assert(!brick::short_brick_floor(world,c));
    world.rise=1.125f;assert(!brick::short_brick_floor(world,c));
    world.rise=std::numeric_limits<float>::quiet_NaN();assert(!brick::short_brick_floor(world,c));
    world.rise=0.375f;
    assert(!brick::short_brick_floor(world,Coordinate{-1,3,1}));
    assert(brick::lower_gap(world,Coordinate{3,3,2},0.75f,inactive)==0.75f);
    brick::Context active{70,{1,1,0},{4,4,0}};
    world.cap=false;
    assert(brick::lower_gap(world,c,0.75f,active)==0.1875f);
    assert(brick::lower_gap(world,Coordinate{5,5,1},0.75f,active)==0.1875f);
    assert(brick::lower_gap(world,Coordinate{6,5,1},0.75f,active)==0.75f);
    assert(brick::lower_gap(world,Coordinate{3,3,0},0.75f,active)==0.75f);
    auto snapshot=brick::ground_snapshot(world);
    assert(snapshot.size()==49);
    for(const auto& sample:snapshot)assert(world.vertex_valid(sample.point)&&sample.point.z==0&&sample.height==0.0f);
    {brick::Context high{71,{1,1,0},{4,4,0}};assert(brick::lower_gap(world,c,0.75f,high)==0.75f);}
    // The candidate changes the minimum only; it must not move either plane.
    assert(world.rise==0.375f);
    // Emulate the native lower comparison: a 2-step floor over terrain 0
    // violates the 4-step gap, but satisfies the scoped 1-step gap.
    assert(0.0f >= 0.375f-0.75f+0.046875f);
    assert(!(0.0f >= 0.375f-0.1875f+0.046875f));
    puts("PASS: connected brick identification, interior vertices, decks/ordinary floors excluded, bounds, heights, lower-level-only minimum.");

    struct NativeFixture { uintptr_t* vtable; } native_world;
    uintptr_t vtable[100]{};native_world.vtable=vtable;
    vtable[0x178/4]=reinterpret_cast<uintptr_t>(&fake_step);
    calculation_id=70;assert(placement_step(&native_world,nullptr)*4==0.375f&&GetLastError()==1357);
    calculation_id=71;assert(placement_step(&native_world,nullptr)*4==1.125f);
    calculation_id=72;assert(placement_step(&native_world,nullptr)*4==1.5f);
    calculation_id=-1;assert(placement_step(&native_world,nullptr)*4==0.75f);
    context=active;assert(build_step(&native_world,nullptr)*4==0.375f);
    context={71,{1,1,0},{4,4,0}};assert(build_step(&native_world,nullptr)*4==0.75f);
    context={70,{1,1,1},{4,4,1}};assert(build_step(&native_world,nullptr)*4==0.75f);
    puts("PASS: exact 2/6/8-step rises, default 4-step rise, native getter ABI, upper-level exclusions.");

    BYTE tool[0x100]{};
    *reinterpret_cast<int*>(tool+0x74)=15;*reinterpret_cast<int*>(tool+0x78)=70;
    Point point{1,2,0};calculation_id=123;
    assert(call_calculation(&point,2,tool)==12.5f);
    assert(observed_point==&point&&observed_category==2&&observed_id==70&&calculation_id==123&&GetLastError()==771);
    // A different runtime key for the same script declaration is accepted.
    *reinterpret_cast<int*>(tool+0x74)=16;*reinterpret_cast<int*>(tool+0x78)=70;
    assert(call_calculation(&point,2,tool)==12.5f&&observed_id==70);
    *reinterpret_cast<int*>(tool+0x74)=17;*reinterpret_cast<int*>(tool+0x78)=71;
    assert(call_calculation(&point,2,tool)==12.5f&&observed_id==71);
    *reinterpret_cast<int*>(tool+0x74)=19;*reinterpret_cast<int*>(tool+0x78)=72;
    assert(call_calculation(&point,2,tool)==12.5f&&observed_id==72);
    assert(call_calculation(&point,1,tool)==12.5f&&observed_id==-1&&calculation_id==123);
    *reinterpret_cast<int*>(tool+0x78)=16;
    assert(call_calculation(&point,2,tool)==12.5f&&observed_id==-1);
    {IdScope a(70);{IdScope b(71);assert(calculation_id==71);}assert(calculation_id==70);}assert(calculation_id==123);
    Coordinate a{1,1,0},b{4,4,0};context={72,{5,5,0},{6,6,0}};
    selection={15,70};
    assert(commit(15,&a,&b,0.375f,123,reinterpret_cast<void*>(0xabc)));
    assert(context.id==72&&GetLastError()==991);
    context=active;SetLastError(1122);
    assert(call_lower(&c,0.375f,0.75f)==0.1875f);
    assert(second_value==1.0f&&before_flags==after_flags&&GetLastError()==1122);
    assert(!memcmp(before_xmm,after_xmm,sizeof(before_xmm)));
    assert(call_lower(&a,0.0f,0.75f)==-0.75f);
    puts("PASS: x86 calculation adapter, argument/stack preservation, scoped commit and nested contexts, x87/SSE/flags and LastError preservation.");

}
