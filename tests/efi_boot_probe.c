/* TEST ONLY: a trusted launcher supplies hostile options and an unsigned initrd.
 * Never ship or sign this executable with a production key.
 */
#include <efi.h>
#include <efilib.h>

static VOID *initrd;
static UINTN initrd_size;

static void serial(const char *text)
{
    while (*text) {
        __asm__ volatile("outb %0, %1" : : "a"((UINT8)*text++), "Nd"((UINT16)0x3f8));
    }
}

static EFI_STATUS EFIAPI load_initrd(EFI_LOAD_FILE_PROTOCOL *self, EFI_DEVICE_PATH *path,
                                   BOOLEAN policy, UINTN *size, VOID *buffer)
{
    (void)self; (void)path; (void)policy;
    serial("TD_TEST_LOADFILE2_CALLED\n");
    if (!buffer || *size < initrd_size) {
        *size = initrd_size;
        return EFI_BUFFER_TOO_SMALL;
    }
    CopyMem(buffer, initrd, initrd_size);
    *size = initrd_size;
    return EFI_SUCCESS;
}

EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system)
{
    EFI_LOADED_IMAGE *loaded, *child_image;
    EFI_HANDLE child, provider = NULL;
    EFI_STATUS status;
    EFI_GUID load_file2 = {0x4006c0c1, 0xfcb3, 0x403e, {0x99, 0x6d, 0x4a, 0x6c, 0x87, 0x24, 0xe0, 0x6d}};
    struct {
        VENDOR_DEVICE_PATH vendor;
        EFI_DEVICE_PATH end;
    } device = {
        {{MEDIA_DEVICE_PATH, MEDIA_VENDOR_DP, {sizeof(VENDOR_DEVICE_PATH), 0}},
         {0x5568e427, 0x68fc, 0x4f3d, {0xac, 0x74, 0xca, 0x55, 0x52, 0x31, 0xcc, 0x68}}},
        {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {sizeof(EFI_DEVICE_PATH), 0}}
    };
    EFI_LOAD_FILE_PROTOCOL protocol = {load_initrd};
    EFI_FILE_HANDLE root, file;
    EFI_FILE_INFO *info;
    UINT8 secure = 0;
    UINTN size = sizeof(secure);
    InitializeLib(image, system);
    status = uefi_call_wrapper(RT->GetVariable, 5, L"SecureBoot", &EfiGlobalVariable, NULL, &size, &secure);
    serial(!EFI_ERROR(status) && secure ? "TD_TEST_SECURE_BOOT_ON\n" : "TD_TEST_SECURE_BOOT_OFF\n");
    status = uefi_call_wrapper(BS->HandleProtocol, 3, image, &LoadedImageProtocol, (VOID **)&loaded);
    if (EFI_ERROR(status)) return status;
    root = LibOpenRoot(loaded->DeviceHandle);
    if (!root) return EFI_NOT_FOUND;
    status = uefi_call_wrapper(root->Open, 5, root, &file, L"\\evil.cpio", EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
        info = LibFileInfo(file);
        if (!info) return EFI_OUT_OF_RESOURCES;
        initrd_size = info->FileSize;
        initrd = AllocatePool(initrd_size);
        if (!initrd) return EFI_OUT_OF_RESOURCES;
        status = uefi_call_wrapper(file->Read, 3, file, &initrd_size, initrd);
        if (EFI_ERROR(status)) return status;
        status = uefi_call_wrapper(BS->InstallMultipleProtocolInterfaces, 6, &provider,
                                  &DevicePathProtocol, &device, &load_file2, &protocol, NULL);
        if (EFI_ERROR(status)) return status;
        serial("TD_TEST_LOADFILE2_INSTALLED\n");
    }
    status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image,
                              FileDevicePath(loaded->DeviceHandle, L"\\kernel.efi"), NULL, 0, &child);
    if (status == EFI_SECURITY_VIOLATION) {
        serial("TD_TEST_SECURITY_VIOLATION\n");
        return status;
    }
    if (status == EFI_ACCESS_DENIED) {
        serial("TD_TEST_ACCESS_DENIED\n");
        return status;
    }
    if (EFI_ERROR(status)) {
        serial("TD_TEST_UNEXPECTED_LOAD_FAILURE\n");
        return status;
    }
    status = uefi_call_wrapper(BS->HandleProtocol, 3, child, &LoadedImageProtocol, (VOID **)&child_image);
    if (EFI_ERROR(status)) return status;
    static CHAR16 options[] = L"rdinit=/bin/sh init=/bin/sh initrd=\\evil.cpio random.trust_cpu=on";
    child_image->LoadOptions = options;
    child_image->LoadOptionsSize = sizeof(options);
    serial("TD_TEST_START_KERNEL_WITH_HOSTILE_OPTIONS\n");
    return uefi_call_wrapper(BS->StartImage, 3, child, NULL, NULL);
}
