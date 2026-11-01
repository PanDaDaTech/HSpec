// hspec.cpp - HSpec 轻谱 (Pure Win32 C++)
// A lightweight desktop hardware information panel.
// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <winioctl.h>
#include <wincrypt.h>
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <dbt.h>
#include <intrin.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>
#include <stdlib.h>
#include "resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif
#ifndef BusTypeNvme
#define BusTypeNvme ((STORAGE_BUS_TYPE)0x11)
#endif
#ifndef BusTypeUfs
#define BusTypeUfs ((STORAGE_BUS_TYPE)0x13)
#endif

// ===========================================================================
// Small helpers
// ===========================================================================

static void WPrintf(wchar_t* dst, size_t cap, const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(dst, cap, _TRUNCATE, fmt, ap);
    va_end(ap);
}

static void TrimW(wchar_t* s)
{
    if (!s) return;
    wchar_t* p = s;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n > 0) {
        wchar_t c = s[n - 1];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') s[--n] = 0;
        else break;
    }
}

static void ToWide(const char* s, wchar_t* dst, int cap)
{
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!s || !*s) return;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, dst, cap);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, s, -1, dst, cap);
    if (n <= 0) dst[0] = 0;
    else dst[cap - 1] = 0;
}

struct BrandAlias { const wchar_t* en; const wchar_t* zh; };

// Longest phrases first so "Micron Technology" wins over "Micron".
static const BrandAlias kBrandAliases[] = {
    { L"Micron Technology", L"美光" },
    { L"Western Digital", L"西部数据" },
    { L"ASUSTeK COMPUTER INC.", L"华硕" },
    { L"Micro-Star International Co., Ltd.", L"微星" },
    { L"Gigabyte Technology", L"技嘉" },
    { L"Hewlett-Packard", L"惠普" },
    { L"Intel Corporation", L"英特尔" },
    { L"SK hynix", L"海力士" },
    { L"SK Hynix", L"海力士" },
    { L"SKHynix", L"海力士" },
    { L"Hynix", L"海力士" },
    { L"Micron", L"美光" },
    { L"Samsung", L"三星" },
    { L"Kingston", L"金士顿" },
    { L"Crucial", L"英睿达" },
    { L"ADATA", L"威刚" },
    { L"Corsair", L"海盗船" },
    { L"G.Skill", L"芝奇" },
    { L"Team Group", L"十铨" },
    { L"SanDisk", L"闪迪" },
    { L"WDC", L"西部数据" },
    { L"Seagate", L"希捷" },
    { L"Toshiba", L"东芝" },
    { L"Kioxia", L"铠侠" },
    { L"ASUSTeK", L"华硕" },
    { L"ASUS", L"华硕" },
    { L"Micro-Star", L"微星" },
    { L"MSI", L"微星" },
    { L"Gigabyte", L"技嘉" },
    { L"ASRock", L"华擎" },
    { L"Lenovo", L"联想" },
    { L"Dell Inc.", L"戴尔" },
    { L"Dell", L"戴尔" },
    { L"HP", L"惠普" },
    { L"Acer", L"宏碁" },
    { L"Huawei", L"华为" },
    { L"Xiaomi", L"小米" },
    { L"Intel", L"英特尔" },
};

static wchar_t* FindNoCase(wchar_t* s, const wchar_t* find)
{
    size_t n = wcslen(find);
    if (n == 0) return s;
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] &&
               towlower((wint_t)s[i]) == towlower((wint_t)find[i]))
            i++;
        if (i == n) return s;
    }
    return NULL;
}

static void ReplaceAllNoCase(wchar_t* s, int cap, const wchar_t* find,
    const wchar_t* repl)
{
    size_t fl = wcslen(find), rl = wcslen(repl);
    if (fl == 0) return;
    wchar_t* p = s;
    while ((p = FindNoCase(p, find)) != NULL) {
        size_t tail = wcslen(p + fl);
        size_t pos = (size_t)(p - s);
        if (pos + rl + tail + 1 > (size_t)cap) return;
        memmove(p + rl, p + fl, (tail + 1) * sizeof(wchar_t));
        memcpy(p, repl, rl * sizeof(wchar_t));
        p += rl;
    }
}

static void TranslateVendorText(wchar_t* s, int cap)
{
    for (size_t i = 0; i < sizeof(kBrandAliases) / sizeof(kBrandAliases[0]); i++)
        ReplaceAllNoCase(s, cap, kBrandAliases[i].en, kBrandAliases[i].zh);
}

static bool RegStrW(HKEY root, const wchar_t* sub, const wchar_t* name, wchar_t* out, DWORD outChars)
{
    if (!out || outChars == 0) return false;
    out[0] = 0;
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = outChars * sizeof(wchar_t);
    LONG rc = RegQueryValueExW(key, name, NULL, &type, (LPBYTE)out, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        out[0] = 0;
        return false;
    }
    out[outChars - 1] = 0;
    TrimW(out);
    return true;
}

static bool RegDwordW(HKEY root, const wchar_t* sub, const wchar_t* name, DWORD* out)
{
    if (!out) return false;
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = sizeof(DWORD), value = 0;
    LONG rc = RegQueryValueExW(key, name, NULL, &type, (LPBYTE)&value, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return false;
    *out = value;
    return true;
}

static bool RegBinaryW(HKEY root, const wchar_t* sub, const wchar_t* name,
    BYTE** out, DWORD* outSize)
{
    *out = NULL;
    *outSize = 0;
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = 0;
    LONG rc = RegQueryValueExW(key, name, NULL, &type, NULL, &bytes);
    if (rc != ERROR_SUCCESS || type != REG_BINARY || bytes < 8) {
        RegCloseKey(key);
        return false;
    }
    BYTE* buf = (BYTE*)malloc(bytes);
    if (!buf) {
        RegCloseKey(key);
        return false;
    }
    rc = RegQueryValueExW(key, name, NULL, &type, buf, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        free(buf);
        return false;
    }
    *out = buf;
    *outSize = bytes;
    return true;
}

static void FormatCapacity(unsigned long long bytes, wchar_t* out, int cap)
{
    double gb = (double)bytes / 1073741824.0;
    if (gb >= 1000.0) WPrintf(out, (size_t)cap, L"%.1f TB", gb / 1024.0);
    else WPrintf(out, (size_t)cap, L"%.0f GB", gb);
}

// ===========================================================================
// SMBIOS (GetSystemFirmwareTable 'RSMB')
// ===========================================================================

struct SmbiosInfo
{
    wchar_t biosVendor[128];
    wchar_t biosVersion[192];
    wchar_t biosDate[64];

    wchar_t sysManufacturer[128];
    wchar_t sysProduct[192];
    wchar_t sysVersion[96];
    wchar_t sysSerial[96];
    wchar_t sysSku[96];

    wchar_t boardManufacturer[128];
    wchar_t boardProduct[192];
    wchar_t boardVersion[96];
    wchar_t boardSerial[96];

    wchar_t cpuVersion[192];
    int cpuMaxMhz;

    int memCount;
    struct MemModule {
        wchar_t manufacturer[64];
        wchar_t part[96];
        unsigned long long bytes;
        int speed;
        int type;
    } mem[16];
};

static void SmbiosGetStr(const BYTE* strtab, const BYTE* strend, BYTE index, wchar_t* out, int cap)
{
    out[0] = 0;
    if (index == 0 || strtab >= strend) return;
    const char* p = (const char*)strtab;
    for (BYTE i = 1; i < index; i++) {
        while (p < (const char*)strend && *p) p++;
        if (p >= (const char*)strend) return;
        p++;
    }
    if (p < (const char*)strend) {
        ToWide(p, out, cap);
        TrimW(out);
    }
}

static void SmbiosRegFallback(SmbiosInfo* si)
{
    static const wchar_t* k = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    if (!si->biosVendor[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BIOSVendor", si->biosVendor, 128);
    if (!si->biosVersion[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BIOSVersion", si->biosVersion, 192);
    if (!si->biosDate[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BIOSReleaseDate", si->biosDate, 64);
    if (!si->sysManufacturer[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"SystemManufacturer", si->sysManufacturer, 128);
    if (!si->sysProduct[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"SystemProductName", si->sysProduct, 192);
    if (!si->sysVersion[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"SystemVersion", si->sysVersion, 96);
    if (!si->boardManufacturer[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BaseBoardManufacturer", si->boardManufacturer, 128);
    if (!si->boardProduct[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BaseBoardProduct", si->boardProduct, 192);
    if (!si->boardVersion[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BaseBoardVersion", si->boardVersion, 96);
}

static bool SmbiosTableLooksValid(const BYTE* p, const BYTE* end)
{
    return p + 4 <= end && p[0] <= 127 && p[1] >= 4;
}

static void ParseSmbiosTable(const BYTE* table, const BYTE* end, SmbiosInfo* si)
{
    const BYTE* p = table;
    while (p + 4 <= end) {
        BYTE type = p[0];
        BYTE len = p[1];
        if (len < 4) break;
        const BYTE* fmtEnd = p + len;
        if (fmtEnd > end) break;

        const BYTE* strtab = fmtEnd;
        const BYTE* q = strtab;
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) q++;
        const BYTE* strend = q;

        if (type == 0 && len > 0x08) {
            SmbiosGetStr(strtab, strend, p[0x04], si->biosVendor, 128);
            SmbiosGetStr(strtab, strend, p[0x05], si->biosVersion, 192);
            SmbiosGetStr(strtab, strend, p[0x08], si->biosDate, 64);
        } else if (type == 1 && len > 0x08) {
            SmbiosGetStr(strtab, strend, p[0x04], si->sysManufacturer, 128);
            SmbiosGetStr(strtab, strend, p[0x05], si->sysProduct, 192);
            SmbiosGetStr(strtab, strend, p[0x06], si->sysVersion, 96);
            SmbiosGetStr(strtab, strend, p[0x07], si->sysSerial, 96);
            if (len > 0x1A) SmbiosGetStr(strtab, strend, p[0x19], si->sysSku, 96);
        } else if (type == 2 && len > 0x08) {
            SmbiosGetStr(strtab, strend, p[0x04], si->boardManufacturer, 128);
            SmbiosGetStr(strtab, strend, p[0x05], si->boardProduct, 192);
            SmbiosGetStr(strtab, strend, p[0x06], si->boardVersion, 96);
            SmbiosGetStr(strtab, strend, p[0x07], si->boardSerial, 96);
        } else if (type == 4 && len > 0x25) {
            SmbiosGetStr(strtab, strend, p[0x10], si->cpuVersion, 192);
            si->cpuMaxMhz = (int)*(WORD*)(p + 0x14);
        } else if (type == 17 && len >= 0x17 && si->memCount < 16) {
            unsigned long long memBytes = 0;
            WORD sizeMb = *(WORD*)(p + 0x0C);
            if (sizeMb == 0x7FFF && len >= 0x20) {
                DWORD extMb = *(DWORD*)(p + 0x1C) & 0x7FFFFFFF;
                memBytes = (unsigned long long)extMb * 1024ULL * 1024ULL;
            } else if (sizeMb > 0 && sizeMb != 0xFFFF) {
                memBytes = (unsigned long long)sizeMb * 1024ULL * 1024ULL;
            }
            if (memBytes > 0) {
                SmbiosInfo::MemModule& m = si->mem[si->memCount];
                memset(&m, 0, sizeof(m));
                m.bytes = memBytes;
                m.type = (len > 0x12) ? p[0x12] : 0;
                m.speed = (int)*(WORD*)(p + 0x15);
                if (m.speed == 0xFFFF && len >= 0x58) {
                    // SMBIOS 3.2 extended speed lives at 0x54.
                    DWORD extSpeed = *(DWORD*)(p + 0x54);
                    if (extSpeed > 0 && extSpeed != 0xFFFFFFFF) m.speed = (int)extSpeed;
                }
                if (len > 0x17) SmbiosGetStr(strtab, strend, p[0x17], m.manufacturer, 64);
                if (len > 0x1A) SmbiosGetStr(strtab, strend, p[0x1A], m.part, 96);
                si->memCount++;
            }
        }

        p = (q + 1 < end) ? q + 2 : end;
    }
}

static bool ParseSmbios(SmbiosInfo* si)
{
    memset(si, 0, sizeof(*si));
    bool parsed = false;

    DWORD size = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
    if (size >= 8) {
        BYTE* buf = (BYTE*)malloc(size);
        if (buf) {
            DWORD got = GetSystemFirmwareTable('RSMB', 0, buf, size);
            if (got >= 8) {
                DWORD len = *(DWORD*)(buf + 4);
                const BYTE* table = buf + 8;
                const BYTE* end = table + len;
                if (end > buf + got) end = buf + got;
                if (SmbiosTableLooksValid(table, end)) {
                    ParseSmbiosTable(table, end, si);
                    parsed = true;
                }
            }
            free(buf);
        }
    }

    if (!parsed) {
        // Some systems reject the firmware-table API; mssmbios keeps the same
        // RawSMBIOSData blob in the registry.
        BYTE* reg = NULL;
        DWORD regSize = 0;
        if (RegBinaryW(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Services\\mssmbios\\Data",
                L"SMBiosData", &reg, &regSize)) {
            DWORD len = (regSize >= 8) ? *(DWORD*)(reg + 4) : 0;
            if (len > 0 && 8 + len <= regSize &&
                SmbiosTableLooksValid(reg + 8, reg + 8 + len)) {
                ParseSmbiosTable(reg + 8, reg + 8 + len, si);
                parsed = true;
            } else if (SmbiosTableLooksValid(reg, reg + regSize)) {
                ParseSmbiosTable(reg, reg + regSize, si);
                parsed = true;
            }
            free(reg);
        }
    }

    SmbiosRegFallback(si);
    return parsed;
}

static const wchar_t* MemTypeName(int type)
{
    switch (type) {
    case 0x03: return L"DRAM";
    case 0x0F: return L"SDRAM";
    case 0x12: return L"DDR";
    case 0x13: return L"DDR2";
    case 0x18: return L"DDR3";
    case 0x1A: return L"DDR4";
    case 0x1E: return L"LPDDR4";
    case 0x1F: return L"NVDIMM";
    case 0x20: return L"HBM";
    case 0x21: return L"HBM2";
    case 0x22: return L"DDR5";
    case 0x23: return L"LPDDR5";
    default: return NULL;
    }
}

// ===========================================================================
// CPU
// ===========================================================================

static void GetCpuBrand(wchar_t* out, int cap)
{
    out[0] = 0;
    RegStrW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString", out, (DWORD)cap);
    TrimW(out);
#if defined(_M_IX86) || defined(_M_X64)
    if (!out[0]) {
        int regs[4] = { 0 };
        __cpuid(regs, 0x80000000);
        if ((unsigned)regs[0] >= 0x80000004) {
            char brand[49] = { 0 };
            for (int i = 0; i < 3; i++) {
                __cpuid(regs, 0x80000002 + i);
                memcpy(brand + i * 16, regs, 16);
            }
            ToWide(brand, out, cap);
            TrimW(out);
        }
    }
#endif
}

static int GetPhysicalCores(void)
{
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    if (len == 0) return 0;
    BYTE* buf = (BYTE*)malloc(len);
    if (!buf) return 0;
    int count = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore,
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len)) {
        DWORD off = 0;
        while (off + 8 <= len) {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* e =
                (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buf + off);
            if (e->Relationship == RelationProcessorCore) count++;
            if (e->Size == 0) break;
            off += e->Size;
        }
    }
    free(buf);
    return count;
}

static int GetLogicalCpus(void)
{
    typedef DWORD (WINAPI *PFN_GetActiveProcessorCount)(WORD);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    if (k) {
        PFN_GetActiveProcessorCount fn =
            (PFN_GetActiveProcessorCount)GetProcAddress(k, "GetActiveProcessorCount");
        if (fn) {
            DWORD n = fn(0xFFFF); // ALL_PROCESSOR_GROUPS
            if (n > 0) return (int)n;
        }
    }
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}

// ===========================================================================
// Firmware / Secure Boot
// ===========================================================================

static bool EnablePrivilege(const wchar_t* name)
{
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    bool ok = false;
    LUID luid;
    if (LookupPrivilegeValueW(NULL, name, &luid)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL) != FALSE
            && GetLastError() == ERROR_SUCCESS;
    }
    CloseHandle(token);
    return ok;
}

// 1 = BIOS/Legacy, 2 = UEFI, 0 = unknown
static int GetFirmwareKind(void)
{
    typedef BOOL (WINAPI *PFN_GetFirmwareType)(int*);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    if (k) {
        PFN_GetFirmwareType fn = (PFN_GetFirmwareType)GetProcAddress(k, "GetFirmwareType");
        int ft = 0;
        if (fn && fn(&ft)) return ft;
    }
    DWORD v = 0;
    if (RegDwordW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control",
            L"PEFirmwareType", &v))
        return (int)v;
    return 0;
}

// 1 = on, 0 = off, -1 = unknown
static int GetSecureBootState(void)
{
    DWORD v = 0;
    if (RegDwordW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
            L"UEFISecureBootEnabled", &v))
        return v ? 1 : 0;

    if (EnablePrivilege(L"SeSystemEnvironmentPrivilege")) {
        BYTE b = 0;
        DWORD n = GetFirmwareEnvironmentVariableW(L"SecureBoot",
            L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}", &b, 1);
        if (n == 1) return b ? 1 : 0;
    }
    return -1;
}

static const GUID kEfiCertX509 =
    { 0xa5c059a1, 0x94e4, 0x4aa7, { 0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72 } };

// Returns the currently installed Secure Boot CA generation (2023 preferred,
// otherwise 2011). Only the allowed-signature database (db) is inspected;
// dbx contains revoked certificates and must not be reported as current.
static bool CollectBootCert(wchar_t* out, int cap)
{
    if (!out || cap <= 0) return false;
    out[0] = 0;
    if (!EnablePrivilege(L"SeSystemEnvironmentPrivilege")) return false;

    const DWORD kBufSize = 128 * 1024;
    BYTE* buf = (BYTE*)malloc(kBufSize);
    if (!buf) return false;

    bool has2011 = false, has2023 = false;
    wchar_t other[64] = { 0 };

    SetLastError(ERROR_SUCCESS);
    DWORD n = GetFirmwareEnvironmentVariableW(L"db",
        L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}", buf, kBufSize);
    if (n > 0 && n <= kBufSize) {
        DWORD off = 0;
        while (off + 28 <= n) {
            const BYTE* list = buf + off;
            UINT32 listSize = *(UINT32*)(list + 16);
            UINT32 hdrSize = *(UINT32*)(list + 20);
            UINT32 sigSize = *(UINT32*)(list + 24);
            if (listSize < 28 || off + listSize > n || sigSize == 0) break;

            GUID type;
            memcpy(&type, list, sizeof(GUID));
            DWORD p = off + 28 + hdrSize;
            DWORD listEnd = off + listSize;

            while (p + sigSize <= listEnd) {
                if (IsEqualGUID(type, kEfiCertX509)) {
                    PCCERT_CONTEXT cert = CertCreateCertificateContext(
                        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, buf + p, sigSize);
                    if (cert) {
                        wchar_t cn[256] = { 0 };
                        CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL,
                            cn, 256);
                        if (wcsstr(cn, L"2023")) has2023 = true;
                        else if (wcsstr(cn, L"2011")) has2011 = true;
                        else if (!other[0] && cn[0])
                            wcsncpy_s(other, 64, cn, _TRUNCATE);
                        CertFreeCertificateContext(cert);
                    }
                }
                p += sigSize;
            }
            off += listSize;
        }
    }
    free(buf);

    if (has2023) wcscpy_s(out, (size_t)cap, L"Pca 2023");
    else if (has2011) wcscpy_s(out, (size_t)cap, L"Pca 2011");
    else if (other[0]) wcsncpy_s(out, (size_t)cap, other, _TRUNCATE);
    else return false;
    return true;
}
    return count;
}

// ===========================================================================
// Disks
// ===========================================================================

struct DiskInfo
{
    wchar_t model[160];
    wchar_t bus[16];
    wchar_t style[8];
    unsigned long long bytes;
};

struct DiskVendor { const wchar_t* en; const wchar_t* zh; };

static const DiskVendor kDiskVendors[] = {
    { L"SK hynix", L"海力士" },
    { L"SK Hynix", L"海力士" },
    { L"SK_Hynix", L"海力士" },
    { L"SKHynix", L"海力士" },
    { L"Hynix", L"海力士" },
    { L"Samsung", L"三星" },
    { L"Seagate", L"希捷" },
    { L"Western Digital", L"西部数据" },
    { L"WDC", L"西部数据" },
    { L"Toshiba", L"东芝" },
    { L"Kioxia", L"铠侠" },
    { L"Kingston", L"金士顿" },
    { L"Crucial", L"英睿达" },
    { L"Micron", L"美光" },
    { L"SanDisk", L"闪迪" },
    { L"Intel", L"英特尔" },
    { L"Lenovo", L"联想" },
    { L"LITEON", L"建兴" },
    { L"Plextor", L"浦科特" },
    { L"Netac", L"朗科" },
    { L"Teclast", L"台电" },
};

// Localize the leading vendor token of a disk model string.
static void LocalizeDiskVendor(wchar_t* model)
{
    for (size_t i = 0; i < sizeof(kDiskVendors) / sizeof(kDiskVendors[0]); i++) {
        const wchar_t* key = kDiskVendors[i].en;
        size_t n = wcslen(key);
        if (_wcsnicmp(model, key, n) != 0) continue;
        const wchar_t* rest = model + n;
        while (*rest == L'_' || *rest == L' ' || *rest == L'-') rest++;
        wchar_t tmp[160];
        if (*rest)
            WPrintf(tmp, 160, L"%s %s", kDiskVendors[i].zh, rest);
        else
            wcscpy_s(tmp, 160, kDiskVendors[i].zh);
        wcsncpy_s(model, 160, tmp, _TRUNCATE);
        return;
    }
}

static const wchar_t* BusTypeName(STORAGE_BUS_TYPE t)
{
    switch (t) {
    case BusTypeScsi: return L"SCSI";
    case BusTypeAtapi: return L"ATAPI";
    case BusTypeAta: return L"ATA";
    case BusType1394: return L"1394";
    case BusTypeSsa: return L"SSA";
    case BusTypeFibre: return L"FIBRE";
    case BusTypeUsb: return L"USB";
    case BusTypeRAID: return L"RAID";
    case BusTypeiScsi: return L"ISCSI";
    case BusTypeSas: return L"SAS";
    case BusTypeSata: return L"SATA";
    case BusTypeSd: return L"SD";
    case BusTypeMmc: return L"MMC";
    case BusTypeVirtual: return L"VIRTUAL";
    case BusTypeFileBackedVirtual: return L"VIRTUAL";
    case BusTypeSpaces: return L"SPACES";
    case BusTypeNvme: return L"NVME";
    case BusTypeUfs: return L"UFS";
    default: return L"UNKNOWN";
    }
}

static int EnumDisks(DiskInfo* out, int cap)
{
    int count = 0;
    for (int i = 0; i < 16 && count < cap; i++) {
        wchar_t path[64];
        WPrintf(path, 64, L"\\\\.\\PhysicalDrive%d", i);
        // Access 0 is enough: geometry / property / layout IOCTLs are
        // FILE_ANY_ACCESS, so the panel works without elevation.
        HANDLE h = CreateFileW(path, 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        DiskInfo di;
        memset(&di, 0, sizeof(di));
        DWORD ret = 0;

        BYTE geoBuf[4096];
        memset(geoBuf, 0, sizeof(geoBuf));
        if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                geoBuf, sizeof(geoBuf), &ret, NULL)) {
            // Elevated fallback, e.g. unusual storage drivers.
            GET_LENGTH_INFORMATION len;
            memset(&len, 0, sizeof(len));
            if (!DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                    &len, sizeof(len), &ret, NULL)) {
                CloseHandle(h);
                continue;
            }
            di.bytes = (unsigned long long)len.Length.QuadPart;
        } else {
            DISK_GEOMETRY_EX* geo = (DISK_GEOMETRY_EX*)geoBuf;
            di.bytes = (unsigned long long)geo->DiskSize.QuadPart;
        }
        if (di.bytes == 0) { CloseHandle(h); continue; }

        BYTE descBuf[1024];
        memset(descBuf, 0, sizeof(descBuf));
        STORAGE_PROPERTY_QUERY q;
        memset(&q, 0, sizeof(q));
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                descBuf, sizeof(descBuf), &ret, NULL)) {
            STORAGE_DEVICE_DESCRIPTOR* d = (STORAGE_DEVICE_DESCRIPTOR*)descBuf;
            wcscpy_s(di.bus, 16, BusTypeName(d->BusType));
            wchar_t v[96] = { 0 }, p[128] = { 0 };
            if (d->VendorIdOffset != 0 && d->VendorIdOffset < sizeof(descBuf))
                ToWide((const char*)descBuf + d->VendorIdOffset, v, 96);
            if (d->ProductIdOffset != 0 && d->ProductIdOffset < sizeof(descBuf))
                ToWide((const char*)descBuf + d->ProductIdOffset, p, 128);
            TrimW(v); TrimW(p);
            if (p[0]) {
                if (v[0] && wcsstr(p, v) == NULL)
                    WPrintf(di.model, 160, L"%s %s", v, p);
                else
                    wcsncpy_s(di.model, 160, p, _TRUNCATE);
            } else {
                wcsncpy_s(di.model, 160, v, _TRUNCATE);
            }
        }
        if (!di.bus[0]) wcscpy_s(di.bus, 16, L"UNKNOWN");

        BYTE layoutBuf[16384];
        memset(layoutBuf, 0, sizeof(layoutBuf));
        if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0,
                layoutBuf, sizeof(layoutBuf), &ret, NULL)) {
            DWORD style = *(DWORD*)layoutBuf;
            if (style == PARTITION_STYLE_GPT) wcscpy_s(di.style, 8, L"GPT");
            else if (style == PARTITION_STYLE_MBR) wcscpy_s(di.style, 8, L"MBR");
            else wcscpy_s(di.style, 8, L"RAW");
        } else {
            wcscpy_s(di.style, 8, L"UNKNOWN");
        }

        if (!di.model[0]) wcscpy_s(di.model, 160, L"Disk");
        LocalizeDiskVendor(di.model);
        out[count++] = di;
        CloseHandle(h);
    }
    return count;
}

// ===========================================================================
// UI model
// ===========================================================================

enum IconKind { ICON_CPU = 0, ICON_BOARD, ICON_RAM, ICON_DISK };

struct UiRow
{
    int icon;
    wchar_t title[256];
    wchar_t details[4][256];
    int detailCount;
};

static UiRow g_rows[24];
static int g_rowCount = 0;
static SmbiosInfo g_smbios;

static UiRow* ModelAddRow(int icon, const wchar_t* title)
{
    if (g_rowCount >= 24) return &g_rows[23];
    UiRow* r = &g_rows[g_rowCount++];
    memset(r, 0, sizeof(*r));
    r->icon = icon;
    wcsncpy_s(r->title, 256, title ? title : L"", _TRUNCATE);
    return r;
}

static void ModelAddDetail(UiRow* r, const wchar_t* text)
{
    if (!r || r->detailCount >= 4) return;
    wcsncpy_s(r->details[r->detailCount++], 256, text ? text : L"", _TRUNCATE);
}

static void BuildModel(void)
{
    g_rowCount = 0;
    memset(g_rows, 0, sizeof(g_rows));
    memset(&g_smbios, 0, sizeof(g_smbios));
    ParseSmbios(&g_smbios);
    SmbiosRegFallback(&g_smbios);

    // ---- CPU ----
    wchar_t cpuBrand[192] = { 0 };
    GetCpuBrand(cpuBrand, 192);
    if (!cpuBrand[0] && g_smbios.cpuVersion[0])
        wcsncpy_s(cpuBrand, 192, g_smbios.cpuVersion, _TRUNCATE);
    int cores = GetPhysicalCores();
    int threads = GetLogicalCpus();

    UiRow* r = ModelAddRow(ICON_CPU, cpuBrand[0] ? cpuBrand : L"处理器");
    if (cores > 0) {
        wchar_t d[128];
        if (threads > cores)
            WPrintf(d, 128, L"核心: %d    线程: %d", cores, threads);
        else
            WPrintf(d, 128, L"核心: %d", cores);
        ModelAddDetail(r, d);
    }

    // ---- Board / firmware ----
    wchar_t boardTitle[256] = L"主板";
    const wchar_t* boardProduct = g_smbios.boardProduct[0] ? g_smbios.boardProduct
        : (g_smbios.sysProduct[0] ? g_smbios.sysProduct : L"");
    if (boardProduct[0])
        WPrintf(boardTitle, 256, L"主板: %s", boardProduct);
    r = ModelAddRow(ICON_BOARD, boardTitle);

    if (g_smbios.boardManufacturer[0]) {
        wchar_t mfr[128];
        wcsncpy_s(mfr, 128, g_smbios.boardManufacturer, _TRUNCATE);
        TranslateVendorText(mfr, 128);
        wchar_t d[256];
        WPrintf(d, 256, L"厂商: %s", mfr);
        ModelAddDetail(r, d);
    }
    if (g_smbios.biosVersion[0]) {
        wchar_t d[256];
        WPrintf(d, 256, L"固件: %s", g_smbios.biosVersion);
        ModelAddDetail(r, d);
    }

    int fw = GetFirmwareKind();
    int sb = (fw == 2) ? GetSecureBootState() : -1;
    wchar_t boot[128];
    if (fw == 2) {
        if (sb == 1) wcscpy_s(boot, 128, L"UEFI (Secure Boot)");
        else if (sb == 0) wcscpy_s(boot, 128, L"UEFI (Secure Boot 关闭)");
        else wcscpy_s(boot, 128, L"UEFI");
    } else if (fw == 1) {
        wcscpy_s(boot, 128, L"Legacy BIOS");
    } else {
        wcscpy_s(boot, 128, L"未知");
    }
    {
        wchar_t d[192];
        WPrintf(d, 192, L"启动: %s", boot);
        ModelAddDetail(r, d);
    }

    if (fw == 2 && sb == 1) {
        wchar_t cert[64] = { 0 };
        if (CollectBootCert(cert, 64)) {
            wchar_t d[128];
            WPrintf(d, 128, L"证书: %s", cert);
            ModelAddDetail(r, d);
        }
    }

    // ---- Memory ----
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    unsigned long long smbTotal = 0;
    for (int i = 0; i < g_smbios.memCount; i++) smbTotal += g_smbios.mem[i].bytes;

    unsigned long long totalBytes = smbTotal;
    if (totalBytes == 0) {
        ULONGLONG installedKb = 0;
        if (GetPhysicallyInstalledSystemMemory(&installedKb) && installedKb > 0)
            totalBytes = installedKb * 1024ULL;
    }
    if (totalBytes == 0) totalBytes = ms.ullTotalPhys;

    wchar_t memTitle[256];
    const wchar_t* memType = L"";
    const wchar_t* firstPart = L"";
    int firstSpeed = 0;
    if (g_smbios.memCount > 0) {
        memType = MemTypeName(g_smbios.mem[0].type);
        if (!memType) memType = L"";
        firstPart = g_smbios.mem[0].part;
        firstSpeed = g_smbios.mem[0].speed;
    }
    double totalGb = (double)totalBytes / 1073741824.0;
    if (memType[0])
        WPrintf(memTitle, 256, L"内存: %.0f GB %s", totalGb, memType);
    else
        WPrintf(memTitle, 256, L"内存: %.0f GB", totalGb);

    r = ModelAddRow(ICON_RAM, memTitle);

    if (g_smbios.memCount > 0) {
        wchar_t m0[128] = { 0 };
        wcsncpy_s(m0, 128, g_smbios.mem[0].manufacturer, _TRUNCATE);
        TranslateVendorText(m0, 128);
        if (m0[0]) {
            wchar_t d[256];
            wchar_t m1[128] = { 0 };
            if (g_smbios.memCount > 1 && g_smbios.mem[1].manufacturer[0]) {
                wcsncpy_s(m1, 128, g_smbios.mem[1].manufacturer, _TRUNCATE);
                TranslateVendorText(m1, 128);
            }
            if (m1[0] && wcscmp(m1, m0) != 0)
                WPrintf(d, 256, L"品牌: %s / %s", m0, m1);
            else
                WPrintf(d, 256, L"品牌: %s", m0);
            ModelAddDetail(r, d);
        }
    }

    if (firstPart[0]) {
        wchar_t memDetail[256] = { 0 };
        wcsncpy_s(memDetail, 256, firstPart, _TRUNCATE);
        if (g_smbios.memCount > 1 && g_smbios.mem[1].part[0]
            && wcscmp(g_smbios.mem[1].part, firstPart) != 0) {
            wcscat_s(memDetail, 256, L" / ");
            wcsncat_s(memDetail, 256, g_smbios.mem[1].part, _TRUNCATE);
        }
        if (firstSpeed > 0) {
            wchar_t d[320];
            WPrintf(d, 320, L"型号: %s    %d MHz", memDetail, firstSpeed);
            ModelAddDetail(r, d);
        } else {
            wchar_t d[320];
            WPrintf(d, 320, L"型号: %s", memDetail);
            ModelAddDetail(r, d);
        }
    }

    // ---- Disks ----
    DiskInfo disks[8];
    memset(disks, 0, sizeof(disks));
    int dc = EnumDisks(disks, 6);
    if (dc == 0) {
        r = ModelAddRow(ICON_DISK, L"硬盘: 未检测到");
        (void)r;
    } else {
        for (int i = 0; i < dc; i++) {
            wchar_t title[200];
            WPrintf(title, 200, L"硬盘: %s", disks[i].model);
            r = ModelAddRow(ICON_DISK, title);
            wchar_t capText[32];
            FormatCapacity(disks[i].bytes, capText, 32);
            wchar_t d[256];
            WPrintf(d, 256, L"容量: %s    格式: %s    类型: %s",
                capText, disks[i].style, disks[i].bus);
            ModelAddDetail(r, d);
        }
    }
}

// ===========================================================================
// Rendering
// ===========================================================================

static ULONG_PTR g_gdiToken = 0;
static HWND g_hwnd = NULL;
static UINT g_dpi = 96;
static int g_winW = 420;
static int g_winH = 320;
static bool g_fontReady = false;
static HANDLE g_fontReg = NULL;
static ULONGLONG g_wallpaperStamp = 0;
static NOTIFYICONDATAW g_nid;
static bool g_trayAdded = false;
static UINT g_taskbarCreatedMsg = 0;

static const Gdiplus::Color kTitleColorDark(255, 26, 26, 26);
static const Gdiplus::Color kDetailColorDark(255, 58, 58, 58);
static const Gdiplus::Color kIconColorDark(255, 38, 38, 38);
static const Gdiplus::Color kTitleColorLight(255, 245, 245, 245);
static const Gdiplus::Color kDetailColorLight(255, 205, 205, 205);
static const Gdiplus::Color kIconColorLight(255, 235, 235, 235);

static bool g_lightText = false;
static Gdiplus::Color g_titleColor = kTitleColorDark;
static Gdiplus::Color g_detailColor = kDetailColorDark;
static Gdiplus::Color g_iconColor = kIconColorDark;

static void ApplyPalette(void)
{
    if (g_lightText) {
        g_titleColor = kTitleColorLight;
        g_detailColor = kDetailColorLight;
        g_iconColor = kIconColorLight;
    } else {
        g_titleColor = kTitleColorDark;
        g_detailColor = kDetailColorDark;
        g_iconColor = kIconColorDark;
    }
}

static bool GetWallpaperPath(wchar_t* out, int cap)
{
    out[0] = 0;
    // SPI_GETDESKWALLPAPER is the image currently in use (also for slideshows).
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, (UINT)cap, out, 0) && out[0]) {
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    wchar_t appdata[MAX_PATH] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        WPrintf(out, (size_t)cap, L"%s\\Microsoft\\Windows\\Themes\\TranscodedWallpaper",
            appdata);
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    RegStrW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper", out, (DWORD)cap);
    return out[0] != 0;
}

static ULONGLONG HashStamp(const wchar_t* path, ULONGLONG h)
{
    if (!path || !path[0]) return h;
    for (const wchar_t* c = path; *c; c++)
        h = (h ^ (ULONGLONG)(unsigned)*c) * 1099511628211ULL;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return h;
    h = (h ^ (ULONGLONG)fad.ftLastWriteTime.dwLowDateTime) * 1099511628211ULL;
    h = (h ^ (ULONGLONG)fad.ftLastWriteTime.dwHighDateTime) * 1099511628211ULL;
    h = (h ^ (ULONGLONG)fad.nFileSizeLow) * 1099511628211ULL;
    h = (h ^ (ULONGLONG)fad.nFileSizeHigh) * 1099511628211ULL;
    return h;
}

// Changes whenever the active wallpaper image or solid background color does.
static ULONGLONG WallpaperStamp(void)
{
    ULONGLONG h = 1469598103934665603ULL;
    wchar_t path[MAX_PATH] = { 0 };
    if (GetWallpaperPath(path, MAX_PATH)) h = HashStamp(path, h);
    wchar_t bg[64] = { 0 };
    if (RegStrW(HKEY_CURRENT_USER, L"Control Panel\\Colors", L"Background", bg, 64)) {
        for (const wchar_t* c = bg; *c; c++)
            h = (h ^ (ULONGLONG)(unsigned)*c) * 1099511628211ULL;
    }
    return h;
}

// Pick dark or light text from the wallpaper brightness behind the panel.
static void UpdateTheme(void)
{
    wchar_t path[MAX_PATH] = { 0 };
    bool haveImage = false;

    if (GetWallpaperPath(path, MAX_PATH)) {
        Gdiplus::Bitmap img(path);
        if (img.GetLastStatus() == Gdiplus::Ok) {
            int iw = (int)img.GetWidth();
            int ih = (int)img.GetHeight();
            if (iw > 0 && ih > 0) {
                POINT origin = { 0, 0 };
                MONITORINFO mi;
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);

                double s = (double)g_dpi / 96.0;
                int x = mi.rcMonitor.right - g_winW - (int)(18.0 * s + 0.5);
                int y = mi.rcMonitor.top + (int)(22.0 * s + 0.5);
                int mw = mi.rcMonitor.right - mi.rcMonitor.left;
                int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
                if (mw > 0 && mh > 0) {
                    double sx = (double)iw / mw;
                    double sy = (double)ih / mh;
                    double sum = 0;
                    int cnt = 0;
                    for (int gy = 0; gy < 10; gy++) {
                        for (int gx = 0; gx < 10; gx++) {
                            int px = (int)((x + (gx + 0.5) * g_winW / 10.0) * sx);
                            int py = (int)((y + (gy + 0.5) * g_winH / 10.0) * sy);
                            if (px < 0 || py < 0 || px >= iw || py >= ih) continue;
                            Gdiplus::Color c;
                            img.GetPixel(px, py, &c);
                            sum += (c.GetR() * 299 + c.GetG() * 587 + c.GetB() * 114) / 1000.0;
                            cnt++;
                        }
                    }
                    if (cnt > 0) {
                        g_lightText = (sum / cnt) < 118.0;
                        ApplyPalette();
                        haveImage = true;
                    }
                }
            }
        }
    }

    if (!haveImage) {
        // Solid-color desktop: HKCU\Control Panel\Colors\Background = "R G B".
        wchar_t bg[64] = { 0 };
        if (RegStrW(HKEY_CURRENT_USER, L"Control Panel\\Colors", L"Background", bg, 64)) {
            int r = 0, g = 0, b = 0;
            if (swscanf_s(bg, L"%d %d %d", &r, &g, &b) == 3) {
                double avg = r * 0.299 + g * 0.587 + b * 0.114;
                g_lightText = avg < 118.0;
                ApplyPalette();
            }
        }
    }
}

static void LoadEmbeddedFont(HINSTANCE inst)
{
    HRSRC hr = FindResourceW(inst, MAKEINTRESOURCEW(IDR_FONT), MAKEINTRESOURCEW(10));
    if (!hr) return;
    HGLOBAL hg = LoadResource(inst, hr);
    if (!hg) return;
    void* data = LockResource(hg);
    DWORD sz = SizeofResource(inst, hr);
    if (!data || sz == 0) return;

    DWORD n = 0;
    HANDLE reg = AddFontMemResourceEx(data, sz, NULL, &n);
    if (!reg || n == 0) return;
    g_fontReg = reg;
    g_fontReady = true;
}

static UINT GetDpiSafe(HWND hwnd)
{
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        PFN_GetDpiForWindow fn = (PFN_GetDpiForWindow)GetProcAddress(u, "GetDpiForWindow");
        if (fn) {
            UINT d = fn(hwnd);
            if (d > 0) return d;
        }
    }
    HDC dc = GetDC(hwnd);
    UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
    return d ? d : 96;
}

static int g_titleH = 24, g_detailH = 21, g_rowGap = 9, g_iconSize = 24, g_gapX = 10, g_padTop = 4;

static void ComputeLayout(void)
{
    double s = (double)g_dpi / 96.0;
    g_iconSize = (int)(24.0 * s + 0.5);
    g_gapX = (int)(10.0 * s + 0.5);
    g_titleH = (int)(26.0 * s + 0.5);
    g_detailH = (int)(22.0 * s + 0.5);
    g_rowGap = (int)(9.0 * s + 0.5);
    g_padTop = (int)(4.0 * s + 0.5);
    g_winW = (int)(388.0 * s + 0.5);

    int h = g_padTop;
    for (int i = 0; i < g_rowCount; i++)
        h += g_titleH + g_rows[i].detailCount * g_detailH + g_rowGap;
    h += (int)(4.0 * s + 0.5);
    g_winH = h;
}

static void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rc, float r)
{
    if (r <= 0) { path.AddRectangle(rc); return; }
    float d = r * 2;
    path.AddArc(rc.X, rc.Y, d, d, 180, 90);
    path.AddArc(rc.GetRight() - d, rc.Y, d, d, 270, 90);
    path.AddArc(rc.GetRight() - d, rc.GetBottom() - d, d, d, 0, 90);
    path.AddArc(rc.X, rc.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

static void DrawGlyph(Gdiplus::Graphics& g, Gdiplus::Pen& pen, int kind,
    float x, float y, float sz, float s)
{
    const float u = sz / 24.0f;
    switch (kind) {
    case ICON_CPU: {
        // Chip with pins on all four sides, 14x14 body inside the 24 box.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 5 * u, y + 5 * u, 14 * u, 14 * u), 2 * u);
        g.DrawPath(&pen, &p);
        g.DrawRectangle(&pen, x + 9.5f * u, y + 9.5f * u, 5 * u, 5 * u);
        for (int i = 0; i < 3; i++) {
            float t = x + (8.0f + i * 4.0f) * u;
            g.DrawLine(&pen, t, y + 2 * u, t, y + 5 * u);
            g.DrawLine(&pen, t, y + 19 * u, t, y + 22 * u);
            float v = y + (8.0f + i * 4.0f) * u;
            g.DrawLine(&pen, x + 2 * u, v, x + 5 * u, v);
            g.DrawLine(&pen, x + 19 * u, v, x + 22 * u, v);
        }
        break;
    }
    case ICON_BOARD: {
        // Motherboard: wide board, CPU socket, two slots, front-panel header.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 2.5f * u, y + 4 * u, 19 * u, 16 * u), 2 * u);
        g.DrawPath(&pen, &p);
        g.DrawRectangle(&pen, x + 6 * u, y + 8 * u, 6 * u, 5 * u);
        g.DrawLine(&pen, x + 15 * u, y + 8 * u, x + 15 * u, y + 16 * u);
        g.DrawLine(&pen, x + 18 * u, y + 8 * u, x + 18 * u, y + 16 * u);
        g.DrawLine(&pen, x + 6 * u, y + 16.5f * u, x + 12 * u, y + 16.5f * u);
        break;
    }
    case ICON_RAM: {
        // RAM stick: wide body with notches and contact pins.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 2 * u, y + 7 * u, 20 * u, 10 * u), 1.5f * u);
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, x + 8.5f * u, y + 12 * u, x + 8.5f * u, y + 17 * u);
        g.DrawLine(&pen, x + 14.5f * u, y + 12 * u, x + 14.5f * u, y + 17 * u);
        for (int i = 0; i < 5; i++) {
            float t = x + (4.0f + i * 4.0f) * u;
            g.DrawLine(&pen, t, y + 17 * u, t, y + 19 * u);
        }
        break;
    }
    case ICON_DISK: {
        // Drive: rounded body, spindle and two slot lines.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 2 * u, y + 5 * u, 20 * u, 14 * u), 2 * u);
        g.DrawPath(&pen, &p);
        g.DrawEllipse(&pen, x + 6 * u, y + 9 * u, 6 * u, 6 * u);
        g.DrawLine(&pen, x + 15 * u, y + 9 * u, x + 19 * u, y + 9 * u);
        g.DrawLine(&pen, x + 15 * u, y + 13 * u, x + 19 * u, y + 13 * u);
        break;
    }
    default: break;
    }
}

// True when the font can draw every character in the line.  Used to fall
// back to a system CJK font for dynamic strings outside the embedded subset.
static bool FontCoversText(HDC dc, HFONT hf, const wchar_t* text)
{
    HGDIOBJ old = SelectObject(dc, hf);
    bool ok = true;
    WORD glyphs[128];
    const wchar_t* p = text;
    while (*p) {
        int n = (int)wcslen(p);
        if (n > 128) n = 128;
        if (GetGlyphIndicesW(dc, p, n, glyphs, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) {
            ok = false;
            break;
        }
        for (int i = 0; i < n; i++) {
            if (glyphs[i] == 0xFFFF) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
        p += n;
    }
    SelectObject(dc, old);
    return ok;
}

// Render one text line with GDI ClearType on an opaque mask, derive the
// per-pixel alpha from the luminance difference, then composite the
// premultiplied result.  This matches the crispness of native static
// controls (WinosInfo) while keeping the layered window transparent.
static void DrawTextGdiLine(Gdiplus::Graphics& g, const wchar_t* text, int px,
    int weight, const Gdiplus::Color& color, bool lightText,
    int x, int y, int w, int h)
{
    if (!text || !text[0] || w <= 0 || h <= 0) return;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!dc) return;

    void* bits = NULL;
    HBITMAP hbm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) {
        DeleteDC(dc);
        return;
    }

    BYTE bg = lightText ? 0 : 255;
    {
        DWORD fill = (DWORD)bg | ((DWORD)bg << 8) | ((DWORD)bg << 16) | 0xFF000000u;
        DWORD* p = (DWORD*)bits;
        int total = w * h;
        for (int i = 0; i < total; i++) p[i] = fill;
    }

    HBITMAP oldBmp = (HBITMAP)SelectObject(dc, hbm);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(color.GetR(), color.GetG(), color.GetB()));
    HFONT hf = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        g_fontReady ? L"MiSans" : L"Microsoft YaHei");
    if (g_fontReady && hf && !FontCoversText(dc, hf, text)) {
        DeleteObject(hf);
        hf = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    }
    if (!hf) {
        SelectObject(dc, oldBmp);
        DeleteObject(hbm);
        DeleteDC(dc);
        return;
    }
    HGDIOBJ oldFont = SelectObject(dc, hf);
    RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = w;
    rect.bottom = h;
    DrawTextW(dc, text, -1, &rect,
        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    DeleteObject(hf);

    int bl = bg;
    int fl = (color.GetR() * 299 + color.GetG() * 587 + color.GetB() * 114) / 1000;
    int denom = bl - fl;
    if (denom == 0) denom = 1;

    BYTE* p = (BYTE*)bits;
    int total = w * h;
    for (int i = 0; i < total; i++) {
        BYTE* q = p + i * 4;
        int lum = (q[2] * 299 + q[1] * 587 + q[0] * 114) / 1000;
        int a = (bl - lum) * 255 / denom;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        q[0] = (BYTE)(color.GetB() * a / 255);
        q[1] = (BYTE)(color.GetG() * a / 255);
        q[2] = (BYTE)(color.GetR() * a / 255);
        q[3] = (BYTE)a;
    }

    Gdiplus::Bitmap line(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)bits);
    if (line.GetLastStatus() == Gdiplus::Ok)
        g.DrawImage(&line, x, y);

    SelectObject(dc, oldBmp);
    DeleteObject(hbm);
    DeleteDC(dc);
}

static int GetEncoderClsid(const wchar_t* mime, CLSID* clsid)
{
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (!codecs) return -1;
    Gdiplus::GetImageEncoders(num, size, codecs);
    int found = -1;
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(codecs[i].MimeType, mime) == 0) {
            *clsid = codecs[i].Clsid;
            found = (int)i;
            break;
        }
    }
    free(codecs);
    return found;
}

static void DumpPanelIfRequested(const void* bits)
{
    wchar_t path[MAX_PATH] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"HSPEC_DUMP", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH || !bits) return;
    CLSID png;
    if (GetEncoderClsid(L"image/png", &png) < 0) return;
    Gdiplus::Bitmap out(g_winW, g_winH, g_winW * 4, PixelFormat32bppPARGB,
        (BYTE*)bits);
    if (out.GetLastStatus() == Gdiplus::Ok)
        out.Save(path, &png, NULL);
}

static bool RenderInternal(void)
{
    if (!g_hwnd || g_winW <= 0 || g_winH <= 0) return false;
    double s = (double)g_dpi / 96.0;

    Gdiplus::Bitmap bmp(g_winW, g_winH, PixelFormat32bppPARGB);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    Gdiplus::Pen pen(g_iconColor, (Gdiplus::REAL)(1.5 * s));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    int titlePx = (int)(16.0 * s + 0.5);
    int detailPx = (int)(14.0 * s + 0.5);
    if (titlePx < 10) titlePx = 10;
    if (detailPx < 9) detailPx = 9;

    float y = (float)g_padTop;
    float textX = (float)(g_iconSize + g_gapX);
    float textW = (float)g_winW - textX - (float)(4.0 * s);

    for (int i = 0; i < g_rowCount; i++) {
        UiRow& row = g_rows[i];
        float iconY = y + ((float)g_titleH - (float)g_iconSize) * 0.5f;
        DrawGlyph(g, pen, row.icon, 2.0f * (float)s, iconY, (float)g_iconSize, (float)s);
        DrawTextGdiLine(g, row.title, titlePx, FW_BOLD, g_titleColor, g_lightText,
            (int)textX, (int)y, (int)textW, g_titleH);
        y += (float)g_titleH;
        for (int j = 0; j < row.detailCount; j++) {
            DrawTextGdiLine(g, row.details[j], detailPx, FW_NORMAL, g_detailColor,
                g_lightText, (int)textX, (int)y, (int)textW, g_detailH);
            y += (float)g_detailH;
        }
        y += (float)g_rowGap;
    }

    // Bitmap::GetHBITMAP() drops the alpha channel for PARGB bitmaps, so copy
    // the premultiplied pixels into our own 32bpp top-down DIB section.
    Gdiplus::BitmapData bd;
    Gdiplus::Rect lockRect(0, 0, g_winW, g_winH);
    if (bmp.LockBits(&lockRect, Gdiplus::ImageLockModeRead,
            PixelFormat32bppPARGB, &bd) != Gdiplus::Ok)
        return false;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_winW;
    bi.bmiHeader.biHeight = -g_winH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hbm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbm && bits) {
        const BYTE* src = (const BYTE*)bd.Scan0;
        int rowBytes = g_winW * 4;
        if (bd.Stride < 0) {
            for (int y = 0; y < g_winH; y++)
                memcpy((BYTE*)bits + y * rowBytes,
                    src + (g_winH - 1 - y) * (-bd.Stride), rowBytes);
        } else {
            for (int y = 0; y < g_winH; y++)
                memcpy((BYTE*)bits + y * rowBytes, src + y * bd.Stride, rowBytes);
        }
    }
    bmp.UnlockBits(&bd);
    if (!hbm) return false;

    DumpPanelIfRequested(bits);

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP old = (HBITMAP)SelectObject(mem, hbm);

    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    SIZE size;
    size.cx = g_winW;
    size.cy = g_winH;
    POINT src = { 0, 0 };
    BOOL ok = UpdateLayeredWindow(g_hwnd, screen, NULL, &size, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(hbm);
    ReleaseDC(NULL, screen);
    return ok != FALSE;
}

static void PositionWindow(void);

static void Render(void)
{
    if (g_hwnd) RenderInternal();
}

// ===========================================================================
// Desktop layer placement
// ===========================================================================
//
// A desktop widget must sit above the wallpaper/icons (Progman + WorkerW)
// but below every ordinary window, and it must never be topmost.  The
// reference tools (WinosInfo) use exactly this: a normal WS_POPUP layered
// window whose z-order is anchored right above the desktop stack.

struct TopWindowList { HWND hwnds[512]; int count; };

static BOOL CALLBACK CollectTopProc(HWND hwnd, LPARAM lp)
{
    TopWindowList* list = (TopWindowList*)lp;
    if (list->count < 512 && IsWindowVisible(hwnd))
        list->hwnds[list->count++] = hwnd;
    return TRUE;
}

static void SendToDesktopLayer(void)
{
    if (!g_hwnd) return;

    TopWindowList list;
    list.count = 0;
    EnumWindows(CollectTopProc, (LPARAM)&list);

    int desk = -1;
    for (int i = 0; i < list.count; i++) {
        wchar_t cls[64] = { 0 };
        GetClassNameW(list.hwnds[i], cls, 64);
        if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) {
            desk = i;
            break;  // topmost desktop-layer window (icons/wallpaper stack)
        }
    }

    if (desk > 0) {
        HWND above = list.hwnds[desk - 1];
        if (above == g_hwnd) return;
        SetWindowPos(g_hwnd, above, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void PositionWindow(void)
{
    if (!g_hwnd) return;
    double s = (double)g_dpi / 96.0;
    POINT origin = { 0, 0 };
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    int marginX = (int)(18.0 * s + 0.5);
    int marginY = (int)(22.0 * s + 0.5);
    int x = mi.rcMonitor.right - g_winW - marginX;
    int y = mi.rcMonitor.top + marginY;
    if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;

    SetWindowPos(g_hwnd, NULL, x, y, g_winW, g_winH,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

// ===========================================================================
// Tray icon
// ===========================================================================

static void AddTrayIcon(HWND hwnd, HINSTANCE inst)
{
    if (g_trayAdded || !hwnd) return;
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP + 2;
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    g_nid.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_MAIN_ICON),
        IMAGE_ICON, cx, cy, 0);
    if (!g_nid.hIcon)
        g_nid.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wcscpy_s(g_nid.szTip, 128, L"HSpec 轻谱");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

static void RemoveTrayIcon(void)
{
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayAdded = false;
    }
    if (g_nid.hIcon) {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = NULL;
    }
}

static void ShowTrayMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"刷新");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, 2, L"退出");
    SetForegroundWindow(hwnd);
    int cmd = (int)TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    if (cmd == 1) {
        BuildModel();
        ComputeLayout();
        g_wallpaperStamp = WallpaperStamp();
        UpdateTheme();
        PositionWindow();
        Render();
        SendToDesktopLayer();
    } else if (cmd == 2) {
        DestroyWindow(hwnd);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_taskbarCreatedMsg != 0 && msg == g_taskbarCreatedMsg) {
        g_trayAdded = false;
        AddTrayIcon(hwnd, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 60000, NULL);
        SetTimer(hwnd, 2, 800, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == 1) {
            BuildModel();
            ComputeLayout();
            UpdateTheme();
            PositionWindow();
            Render();
            SendToDesktopLayer();
        } else if (wParam == 2) {
            ULONGLONG stamp = WallpaperStamp();
            if (stamp != g_wallpaperStamp) {
                g_wallpaperStamp = stamp;
                UpdateTheme();
                Render();
            }
            // "Show desktop" hides ordinary top-level windows and raises the
            // desktop WorkerW stack; a desktop widget should stay on the
            // wallpaper, so restore it and keep it anchored above that stack.
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                PositionWindow();
                Render();
            }
            SendToDesktopLayer();
        } else if (wParam == 3) {
            KillTimer(hwnd, 3);
            BuildModel();
            ComputeLayout();
            UpdateTheme();
            PositionWindow();
            Render();
            SendToDesktopLayer();
        }
        return 0;

    case WM_APP + 2:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK)
            ShowTrayMenu(hwnd);
        return 0;

    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVNODES_CHANGED ||
            wParam == DBT_DEVICEARRIVAL ||
            wParam == DBT_DEVICEREMOVECOMPLETE) {
            SetTimer(hwnd, 3, 700, NULL);
        }
        return TRUE;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    // Desktop widgets must survive "Show desktop" (Win+D), which minimizes
    // every top-level window.
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) return 0;
        break;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            PostMessageW(hwnd, WM_APP + 1, 0, 0);
            return 0;
        }
        break;

    case WM_APP + 1:
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        PositionWindow();
        Render();
        SendToDesktopLayer();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETTINGCHANGE:
        g_wallpaperStamp = WallpaperStamp();
        UpdateTheme();
        Render();
        return 0;

    case WM_DISPLAYCHANGE:
        g_dpi = GetDpiSafe(hwnd);
        ComputeLayout();
        PositionWindow();
        Render();
        return 0;

    case WM_DPICHANGED_AFTERPARENT:
        g_dpi = GetDpiSafe(hwnd);
        ComputeLayout();
        PositionWindow();
        Render();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);
        RemoveTrayIcon();
        g_hwnd = NULL;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ===========================================================================
// Entry point
// ===========================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    HANDLE mutex = CreateMutexW(NULL, FALSE, L"HSpec_Singleton");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    Gdiplus::GdiplusStartupInput gdiIn;
    if (Gdiplus::GdiplusStartup(&g_gdiToken, &gdiIn, NULL) != Gdiplus::Ok)
        return 1;

    LoadEmbeddedFont(hInstance);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    BuildModel();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"HSpecWindow";
    if (!RegisterClassExW(&wc)) {
        Gdiplus::GdiplusShutdown(g_gdiToken);
        return 1;
    }

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"HSpecWindow", L"HSpec 轻谱", WS_POPUP,
        0, 0, 100, 100, NULL, NULL, hInstance, NULL);
    if (!g_hwnd) {
        Gdiplus::GdiplusShutdown(g_gdiToken);
        return 1;
    }

    g_dpi = GetDpiSafe(g_hwnd);
    ComputeLayout();
    UpdateTheme();
    g_wallpaperStamp = WallpaperStamp();
    PositionWindow();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    Render();
    SendToDesktopLayer();
    AddTrayIcon(g_hwnd, hInstance);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) CloseHandle(mutex);
    if (g_fontReg) {
        RemoveFontMemResourceEx(g_fontReg);
        g_fontReg = NULL;
    }
    Gdiplus::GdiplusShutdown(g_gdiToken);
    return 0;
}
