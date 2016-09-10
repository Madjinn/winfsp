#include <winfsp/winfsp.h>
#include <tlib/testsuite.h>
#include <strsafe.h>
#include "memfs.h"

void *memfs_start_ex(ULONG Flags, ULONG FileInfoTimeout);
void *memfs_start(ULONG Flags);
void memfs_stop(void *data);
PWSTR memfs_volumename(void *data);

extern int NtfsTests;
extern int WinFspDiskTests;
extern int WinFspNetTests;

static void reparse_guid_dotest(ULONG Flags, PWSTR Prefix, ULONG FileInfoTimeout)
{
    void *memfs = memfs_start_ex(Flags, FileInfoTimeout);

    static const GUID reparse_guid =
        { 0x895fc61e, 0x7b91, 0x4677, { 0xba, 0x3e, 0x79, 0x34, 0xed, 0xf2, 0xb7, 0x43 } };
    HANDLE Handle;
    BOOL Success;
    WCHAR FilePath[MAX_PATH];
    union
    {
        //REPARSE_DATA_BUFFER D;
        REPARSE_GUID_DATA_BUFFER G;
        UINT8 B[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    } ReparseDataBuf;
    DWORD Bytes;
    static const char *datstr = "foobar";

    StringCbPrintfW(FilePath, sizeof FilePath, L"%s%s\\file0",
        Prefix ? L"" : L"\\\\?\\GLOBALROOT", Prefix ? Prefix : memfs_volumename(memfs));

    Handle = CreateFileW(FilePath,
        FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
    ASSERT(INVALID_HANDLE_VALUE != Handle);

    ReparseDataBuf.G.ReparseTag = 0x1234;
    ReparseDataBuf.G.ReparseDataLength = (USHORT)strlen(datstr);
    ReparseDataBuf.G.Reserved = 0;
    memcpy(&ReparseDataBuf.G.ReparseGuid, &reparse_guid, sizeof reparse_guid);
    memcpy(ReparseDataBuf.G.GenericReparseBuffer.DataBuffer, datstr, strlen(datstr));

    Success = DeviceIoControl(Handle, FSCTL_SET_REPARSE_POINT,
        &ReparseDataBuf, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE + ReparseDataBuf.G.ReparseDataLength,
        0, 0,
        &Bytes, 0);
    ASSERT(Success);

    Success = DeviceIoControl(Handle, FSCTL_GET_REPARSE_POINT,
        0, 0,
        &ReparseDataBuf, sizeof ReparseDataBuf,
        &Bytes, 0);
    ASSERT(Success);

    ASSERT(ReparseDataBuf.G.ReparseTag == 0x1234);
    ASSERT(ReparseDataBuf.G.ReparseDataLength == strlen(datstr));
    ASSERT(ReparseDataBuf.G.Reserved == 0);
    ASSERT(0 == memcmp(&ReparseDataBuf.G.ReparseGuid, &reparse_guid, sizeof reparse_guid));
    ASSERT(0 == memcmp(ReparseDataBuf.G.GenericReparseBuffer.DataBuffer, datstr, strlen(datstr)));

    CloseHandle(Handle);

    Handle = CreateFileW(FilePath,
        FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, 0, 0);
    ASSERT(INVALID_HANDLE_VALUE == Handle);
    ASSERT(ERROR_CANT_ACCESS_FILE == GetLastError());

    Handle = CreateFileW(FilePath,
        FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
    ASSERT(INVALID_HANDLE_VALUE != Handle);

    ReparseDataBuf.G.ReparseDataLength = 0;

    Success = DeviceIoControl(Handle, FSCTL_DELETE_REPARSE_POINT,
        &ReparseDataBuf, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE + ReparseDataBuf.G.ReparseDataLength,
        0, 0,
        &Bytes, 0);
    ASSERT(Success);

    Success = DeviceIoControl(Handle, FSCTL_GET_REPARSE_POINT,
        0, 0,
        &ReparseDataBuf, sizeof ReparseDataBuf,
        &Bytes, 0);
    ASSERT(!Success);
    ASSERT(ERROR_NOT_A_REPARSE_POINT == GetLastError());

    CloseHandle(Handle);

    Success = DeleteFileW(FilePath);
    ASSERT(Success);

    memfs_stop(memfs);
}

void reparse_guid_test(void)
{
    if (NtfsTests)
    {
        WCHAR DirBuf[MAX_PATH] = L"\\\\?\\";
        GetCurrentDirectoryW(MAX_PATH - 4, DirBuf + 4);
        reparse_guid_dotest(-1, DirBuf, 0);
    }
    if (WinFspDiskTests)
    {
        reparse_guid_dotest(MemfsDisk, 0, 0);
    }
    if (WinFspNetTests)
    {
        reparse_guid_dotest(MemfsNet, L"\\\\memfs\\share", 0);
    }
}

static void reparse_nfs_dotest(ULONG Flags, PWSTR Prefix, ULONG FileInfoTimeout)
{
    void *memfs = memfs_start_ex(Flags, FileInfoTimeout);

    HANDLE Handle;
    BOOL Success;
    WCHAR FilePath[MAX_PATH];
    union
    {
        REPARSE_DATA_BUFFER D;
        //REPARSE_GUID_DATA_BUFFER G;
        UINT8 B[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    } ReparseDataBuf;
    DWORD Bytes;

    StringCbPrintfW(FilePath, sizeof FilePath, L"%s%s\\file0",
        Prefix ? L"" : L"\\\\?\\GLOBALROOT", Prefix ? Prefix : memfs_volumename(memfs));

    Handle = CreateFileW(FilePath,
        FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
    ASSERT(INVALID_HANDLE_VALUE != Handle);

    ReparseDataBuf.D.ReparseTag = IO_REPARSE_TAG_NFS;
    ReparseDataBuf.D.ReparseDataLength = 16;
    ReparseDataBuf.D.Reserved = 0;
    *(PUINT64)(ReparseDataBuf.D.GenericReparseBuffer.DataBuffer +  0) = 0x524843;/* NFS_SPECFILE_CHR */
    *(PUINT32)(ReparseDataBuf.D.GenericReparseBuffer.DataBuffer +  8) = 0x42; /* major */
    *(PUINT32)(ReparseDataBuf.D.GenericReparseBuffer.DataBuffer + 12) = 0x62; /* minor */

    Success = DeviceIoControl(Handle, FSCTL_SET_REPARSE_POINT,
        &ReparseDataBuf, REPARSE_DATA_BUFFER_HEADER_SIZE + ReparseDataBuf.D.ReparseDataLength,
        0, 0,
        &Bytes, 0);
    ASSERT(Success);

    Success = DeviceIoControl(Handle, FSCTL_GET_REPARSE_POINT,
        0, 0,
        &ReparseDataBuf, sizeof ReparseDataBuf,
        &Bytes, 0);
    ASSERT(Success);

    ASSERT(ReparseDataBuf.D.ReparseTag == IO_REPARSE_TAG_NFS);
    ASSERT(ReparseDataBuf.D.ReparseDataLength == 16);
    ASSERT(ReparseDataBuf.D.Reserved == 0);
    ASSERT(*(PUINT64)(ReparseDataBuf.D.GenericReparseBuffer.DataBuffer +  0) == 0x524843);
    ASSERT(*(PUINT32)(ReparseDataBuf.D.GenericReparseBuffer.DataBuffer +  8) == 0x42);
    ASSERT(*(PUINT32)(ReparseDataBuf.D.GenericReparseBuffer.DataBuffer + 12) == 0x62);

    CloseHandle(Handle);

    Handle = CreateFileW(FilePath,
        FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, 0, 0);
    ASSERT(INVALID_HANDLE_VALUE == Handle);
    ASSERT(ERROR_CANT_ACCESS_FILE == GetLastError());

    Handle = CreateFileW(FilePath,
        FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
    ASSERT(INVALID_HANDLE_VALUE != Handle);

    ReparseDataBuf.D.ReparseDataLength = 0;

    Success = DeviceIoControl(Handle, FSCTL_DELETE_REPARSE_POINT,
        &ReparseDataBuf, REPARSE_DATA_BUFFER_HEADER_SIZE + ReparseDataBuf.D.ReparseDataLength,
        0, 0,
        &Bytes, 0);
    ASSERT(Success);

    Success = DeviceIoControl(Handle, FSCTL_GET_REPARSE_POINT,
        0, 0,
        &ReparseDataBuf, sizeof ReparseDataBuf,
        &Bytes, 0);
    ASSERT(!Success);
    ASSERT(ERROR_NOT_A_REPARSE_POINT == GetLastError());

    CloseHandle(Handle);

    Success = DeleteFileW(FilePath);
    ASSERT(Success);

    memfs_stop(memfs);
}

void reparse_nfs_test(void)
{
    if (NtfsTests)
    {
        WCHAR DirBuf[MAX_PATH] = L"\\\\?\\";
        GetCurrentDirectoryW(MAX_PATH - 4, DirBuf + 4);
        reparse_nfs_dotest(-1, DirBuf, 0);
    }
    if (WinFspDiskTests)
    {
        reparse_nfs_dotest(MemfsDisk, 0, 0);
    }
    if (WinFspNetTests)
    {
        reparse_nfs_dotest(MemfsNet, L"\\\\memfs\\share", 0);
    }
}

static void reparse_symlink_dotest(ULONG Flags, PWSTR Prefix, ULONG FileInfoTimeout)
{
    void *memfs = memfs_start_ex(Flags, FileInfoTimeout);

    HANDLE Handle;
    BOOL Success;
    WCHAR FilePath[MAX_PATH], LinkPath[MAX_PATH];
    PUINT8 NameInfoBuf[sizeof(FILE_NAME_INFO) + MAX_PATH];
    PFILE_NAME_INFO PNameInfo = (PVOID)NameInfoBuf;

    StringCbPrintfW(FilePath, sizeof FilePath, L"%s%s\\file0",
        Prefix ? L"" : L"\\\\?\\GLOBALROOT", Prefix ? Prefix : memfs_volumename(memfs));

    StringCbPrintfW(LinkPath, sizeof LinkPath, L"%s%s\\link0",
        Prefix ? L"" : L"\\\\?\\GLOBALROOT", Prefix ? Prefix : memfs_volumename(memfs));

    Success = CreateSymbolicLinkW(LinkPath, FilePath, 0);
    if (Success)
    {
        Handle = CreateFileW(FilePath,
            FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
        ASSERT(INVALID_HANDLE_VALUE != Handle);
        CloseHandle(Handle);

        Handle = CreateFileW(LinkPath,
            FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            0, OPEN_EXISTING, 0, 0);
        ASSERT(INVALID_HANDLE_VALUE != Handle);

        Success = GetFileInformationByHandleEx(Handle, FileNameInfo, PNameInfo, sizeof NameInfoBuf);
        ASSERT(Success);
        if (-1 == Flags)
            ASSERT(PNameInfo->FileNameLength == wcslen(FilePath + 6) * sizeof(WCHAR));
        else if (0 == Prefix)
            ASSERT(PNameInfo->FileNameLength == wcslen(L"\\file0") * sizeof(WCHAR));
        else
            ASSERT(PNameInfo->FileNameLength == wcslen(FilePath + 1) * sizeof(WCHAR));
        if (-1 == Flags)
            ASSERT(0 == memcmp(FilePath + 6, PNameInfo->FileName, PNameInfo->FileNameLength));
        else if (0 == Prefix)
            ASSERT(0 == memcmp(L"\\file0", PNameInfo->FileName, PNameInfo->FileNameLength));
        else
            ASSERT(0 == memcmp(FilePath + 1, PNameInfo->FileName, PNameInfo->FileNameLength));

        CloseHandle(Handle);

        Success = DeleteFileW(LinkPath);
        ASSERT(Success);

        Success = DeleteFileW(FilePath);
        ASSERT(Success);
    }
    else
    {
        ASSERT(ERROR_PRIVILEGE_NOT_HELD == GetLastError());
        FspDebugLog(__FUNCTION__ ": need SE_CREATE_SYMBOLIC_LINK_PRIVILEGE\n");
    }

    memfs_stop(memfs);
}

void reparse_symlink_test(void)
{
    if (NtfsTests)
    {
        WCHAR DirBuf[MAX_PATH] = L"\\\\?\\";
        GetCurrentDirectoryW(MAX_PATH - 4, DirBuf + 4);
        reparse_symlink_dotest(-1, DirBuf, 0);
    }
    if (WinFspDiskTests)
    {
        reparse_symlink_dotest(MemfsDisk, 0, 0);
    }
    if (WinFspNetTests)
    {
        reparse_symlink_dotest(MemfsNet, L"\\\\memfs\\share", 0);
    }
}

void reparse_tests(void)
{
    TEST(reparse_guid_test);
    TEST(reparse_nfs_test);
    TEST(reparse_symlink_test);
}