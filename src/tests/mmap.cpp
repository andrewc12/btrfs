#include "test.h"

using namespace std;

static unique_handle create_section(ACCESS_MASK access, optional<uint64_t> max_size, ULONG prot,
                                    ULONG atts, HANDLE file) {
    NTSTATUS Status;
    HANDLE h;
    LARGE_INTEGER li;

    if (max_size)
        li.QuadPart = max_size.value();

    Status = NtCreateSection(&h, access, nullptr, max_size ? &li : nullptr, prot, atts, file);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    return unique_handle(h);
}

static void* map_view(HANDLE sect, uint64_t off, uint64_t len) {
    NTSTATUS Status;
    void* addr = nullptr;
    LARGE_INTEGER li;
    SIZE_T size = len;

    li.QuadPart = off;

    Status = NtMapViewOfSection(sect, NtCurrentProcess(), &addr, 0, 0, &li, &size,
                                ViewUnmap, 0, PAGE_READONLY);

    if (Status != STATUS_SUCCESS)
        throw ntstatus_error(Status);

    if (!addr)
        throw runtime_error("NtMapViewOfSection returned address of 0.");

    return addr;
}

void test_mmap(const u16string& dir) {
    unique_handle h;

    test("Create empty file", [&]() {
        h = create_file(dir + u"\\mmapempty", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, 0, FILE_CREATED);
    });

    if (h) {
        test("Try to create section on empty file", [&]() {
            exp_status([&]() {
                auto sect = create_section(SECTION_ALL_ACCESS, nullopt, PAGE_READONLY, SEC_COMMIT, h.get());
            }, STATUS_MAPPED_FILE_SIZE_ZERO);
        });

        h.reset();
    }

    test("Create directory", [&]() {
        h = create_file(dir + u"\\mmapdir", MAXIMUM_ALLOWED, 0, 0, FILE_CREATE, FILE_DIRECTORY_FILE, FILE_CREATED);
    });

    if (h) {
        test("Try to create section on directory", [&]() {
            exp_status([&]() {
                auto sect = create_section(SECTION_ALL_ACCESS, nullopt, PAGE_READONLY, SEC_COMMIT, h.get());
            }, STATUS_INVALID_FILE_FOR_SECTION);
        });

        h.reset();
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\mmap1", SYNCHRONIZE | FILE_READ_DATA | FILE_WRITE_DATA, 0, 0,
                        FILE_CREATE, FILE_SYNCHRONOUS_IO_NONALERT, FILE_CREATED);
    });

    if (h) {
        auto data = random_data(4096);

        test("Write to file", [&]() {
            write_file(h.get(), data);
        });

        unique_handle sect;

        test("Create section", [&]() {
            sect = create_section(SECTION_ALL_ACCESS, nullopt, PAGE_READONLY, SEC_COMMIT, h.get());
        });

        void* addr = nullptr;

        test("Map view", [&]() {
            addr = map_view(sect.get(), 0, data.size());
        });

        if (addr) {
            test("Check data in mapping", [&]() {
                if (memcmp(addr, data.data(), data.size()))
                    throw runtime_error("Data in mapping did not match was written.");
            });

            uint32_t num = 0xdeadbeef;

            test("Write to file", [&]() {
                write_file(h.get(), span<uint8_t>((uint8_t*)&num, sizeof(uint32_t)), 0);
            });

            test("Check data in mapping again", [&]() {
                if (*(uint32_t*)addr != num)
                    throw runtime_error("Data in mapping did not match was written.");
            });
        }
    }

    test("Create file", [&]() {
        h = create_file(dir + u"\\mmap2", SYNCHRONIZE | FILE_READ_DATA | FILE_WRITE_DATA, 0, 0,
                        FILE_CREATE, FILE_SYNCHRONOUS_IO_NONALERT, FILE_CREATED);
    });

    if (h) {
        test("Set end of file", [&]() {
            set_end_of_file(h.get(), 4096);
        });

        test("Try to create section larger than file", [&]() {
            exp_status([&]() {
                create_section(SECTION_ALL_ACCESS, 8192, PAGE_READONLY, SEC_COMMIT, h.get());
            }, STATUS_SECTION_TOO_BIG);
        });
    }

    // FIXME - editing file through mapping

    // FIXME - try mapping when file locked
    // FIXME - attempted delete
    // FIXME - attempted truncate
}