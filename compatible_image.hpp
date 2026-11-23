// Required code at the supported UC layout; no filename or whole-file hash.
// Check every dependency before the existing patch-site validation/writes.
#pragma once
namespace compatibility {
struct Signature { unsigned rva, size, fixup_count; BYTE bytes[48]; unsigned fixups[8]; };
static const Signature signatures[]={
    {0x20aab1,33,1,{0xb8,0x16,0xf4,0x14,0x1,0xe8,0xb1,0x1d,0x85,0x0,0x81,0xec,0x90,0x0,0x0,0x0,0x53,0x56,0x57,0xe8,0x6f,0xef,0x18,0x0,0x8b,0x10,0x8b,0xc8,0xff,0x92,0xbc,0x0,0x0},{1}},
    {0xae0dc,36,2,{0x55,0x8b,0xec,0x51,0xff,0x25,0x54,0x53,0xeb,0x01,0x39,0x75,0x17,0x8d,0x4d,0xfc,0xe8,0x79,0xff,0xff,0xff,0x8b,0x00,0x8d,0x4d,0xfc,0xa3,0xe4,0x9b,0x47,0x01,0xe8,0x8f,0xff,0xff,0xff},{6,27}},
    {0x209725,33,0,{0x55,0x8b,0xec,0x83,0xec,0x1c,0x83,0x7d,0x0c,0x04,0x75,0x0b,0xff,0x75,0x08,0xe8,0x07,0x9c,0x61,0x00,0x59,0xc9,0xc3,0x53,0x56,0x57,0xe8,0x98,0x49,0xea,0xff,0x8b,0xd8},{}},
    {0x20bfe9,33,1,{0xb8,0xb5,0xf4,0x14,0x01,0xe8,0x79,0x08,0x85,0x00,0x83,0xec,0x2c,0x56,0xe8,0x11,0xda,0x18,0x00,0x8b,0x10,0x8b,0xc8,0xff,0x52,0x40,0xff,0x75,0x08,0x8b,0x10,0x8b,0xc8},{1}}
};
inline bool read(uintptr_t address, void* result, size_t size) {
    SIZE_T count=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),result,size,&count) && count==size;
}
inline bool accepts(uintptr_t base) {
    IMAGE_DOS_HEADER dos{};
    if(!base || !read(base,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<64 || dos.e_lfanew>0x100000)return false;
    IMAGE_NT_HEADERS32 nt{};
    if(!read(base+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE ||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
       nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER32) || !nt.FileHeader.NumberOfSections || nt.FileHeader.NumberOfSections>96 ||
       nt.OptionalHeader.SizeOfImage<0x209889 || nt.OptionalHeader.SizeOfImage>0x40000000)return false;
    IMAGE_SECTION_HEADER sections[96]{};
    const uintptr_t table=base+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
    if(!read(table,sections,nt.FileHeader.NumberOfSections*sizeof(sections[0])))return false;
    for(const auto& signature : signatures) {
        if(signature.rva>nt.OptionalHeader.SizeOfImage || signature.size>nt.OptionalHeader.SizeOfImage-signature.rva)return false;
        bool executable=false;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
            const auto& section=sections[i];
            const unsigned span=section.Misc.VirtualSize>section.SizeOfRawData?section.Misc.VirtualSize:section.SizeOfRawData;
            if((section.Characteristics&IMAGE_SCN_MEM_EXECUTE) && signature.rva>=section.VirtualAddress &&
               signature.rva-section.VirtualAddress<=span && signature.size<=span-(signature.rva-section.VirtualAddress))executable=true;
        }
        if(!executable)return false;
        BYTE actual[48]{},expected[48]{};
        if(!read(base+signature.rva,actual,signature.size))return false;
        memcpy(expected,signature.bytes,signature.size);
        for(unsigned i=0;i<signature.fixup_count;++i) {
            unsigned value=0;memcpy(&value,expected+signature.fixups[i],4);
            value+=static_cast<unsigned>(base)-0x400000u;
            memcpy(expected+signature.fixups[i],&value,4);
        }
        if(memcmp(actual,expected,signature.size))return false;
    }
    return true;
}
}
