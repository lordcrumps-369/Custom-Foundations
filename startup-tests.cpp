#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cassert>
#include <vector>
#include <utility>
#include <fstream>
#include <iterator>
static HMODULE fixture_module;
extern "C" HMODULE WINAPI FixtureGetModuleHandleW(LPCWSTR) {return fixture_module;}
static LPTHREAD_START_ROUTINE queued_start;
static HANDLE WINAPI FixtureCreateThread(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE fn,LPVOID,DWORD,LPDWORD) { queued_start=fn; return nullptr; }
#define CreateThread FixtureCreateThread
#define GetModuleHandleW FixtureGetModuleHandleW
#include "plugin.cpp"
#undef GetModuleHandleW
#undef CreateThread

int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<BYTE> disk((std::istreambuf_iterator<char>(file)),{});
    assert(disk.size()==46208224);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(disk.data()+reinterpret_cast<IMAGE_DOS_HEADER*>(disk.data())->e_lfanew);
    const unsigned size=nt->OptionalHeader.SizeOfImage;
    auto* image=static_cast<BYTE*>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    assert(image); fixture_module=reinterpret_cast<HMODULE>(image);
    auto load=[&]() {
        memset(image,0,size);memcpy(image,disk.data(),nt->OptionalHeader.SizeOfHeaders);
        const auto* sections=IMAGE_FIRST_SECTION(nt);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
            memcpy(image+sections[i].VirtualAddress,disk.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
        // Model the loader's rebasing of absolute operands in the dependency
        // signatures and the existing vtable checks. No code is executed here.
        for(const auto& s:compatibility::signatures)for(unsigned i=0;i<s.fixup_count;++i) {
            unsigned value;memcpy(&value,image+s.rva+s.fixups[i],4);
            value+=static_cast<unsigned>(reinterpret_cast<uintptr_t>(image))-0x400000u;
            memcpy(image+s.rva+s.fixups[i],&value,4);
        }
        
        initialized=false;
    };
    using Changes=std::vector<std::pair<unsigned,BYTE>>;
    auto start=[&]() {
        std::vector<BYTE> before(image,image+size);
        if(queued_start) {auto fn=queued_start;queued_start=nullptr;fn(nullptr);}else InitializeASI();
        Changes changed;
        for(unsigned i=0;i<size;++i)if(image[i]!=before[i])changed.emplace_back(i,image[i]);
        return changed;
    };
    load();assert(compatibility::accepts(reinterpret_cast<uintptr_t>(image)));
    const Changes baseline=start(); assert(baseline.size()>15);
    assert(image[0x20c0a1]==0xe8);
    assert(image[0xb79d3]==0xe9 && image[0xd6dcb]==0xe8 && image[0x20982b]==0xe8 && image[0xd7856]==0xe8 && image[0x209883]==0xe8);
    // RPC-style LoadLibrary calls DllMain without invoking the export.
    load();queued_start=nullptr;
    assert(DllMain(fixture_module,DLL_PROCESS_ATTACH,nullptr));assert(queued_start);
    assert(start()==baseline);assert(start().empty());
    // Ultimate ASI Loader can call the export before the queued fallback runs.
    load();assert(DllMain(fixture_module,DLL_PROCESS_ATTACH,nullptr));assert(queued_start);
    InitializeASI();assert(start().empty());
    // Whole-file identity differences must no longer prevent activation.
    load();image[0x30]^=0x5a;
    reinterpret_cast<IMAGE_NT_HEADERS32*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew)->OptionalHeader.CheckSum^=0x12345678;
    image[0x1000]^=0x01; // Unrelated code outside every patch/dependency signature.
    assert(start()==baseline);
    // Reinitialization cannot install a second copy.
    assert(start().empty());
    unsigned negatives=0;
    // Every required native dependency is checked before any patch is written.
    for(const auto& s:compatibility::signatures) {
        load();image[s.rva]^=1;assert(start().empty());++negatives;
    }
    const unsigned patches[]={0x20c0a1,0xd6dcb,0x20982b,0xd7856,0x209883,0xb79d3};
    for(unsigned rva:patches) {load();image[rva]^=1;assert(start().empty());++negatives;}
    load();reinterpret_cast<IMAGE_NT_HEADERS32*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew)->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    assert(start().empty());++negatives;
    load();reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew=0x7fffffff;
    assert(start().empty());++negatives;
    load();reinterpret_cast<IMAGE_NT_HEADERS32*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew)->OptionalHeader.SizeOfImage=4096;
    assert(start().empty());++negatives;
    load();assert(start()==baseline);
    assert(!compatibility::accepts(1));
    VirtualFree(image,0,MEM_RELEASE);
    printf("foundations: original startup, unrelated executable edits, repeated initialization, and %u incompatible-code/header cases passed; %u changed bytes.\n",negatives,unsigned(baseline.size()));
}
