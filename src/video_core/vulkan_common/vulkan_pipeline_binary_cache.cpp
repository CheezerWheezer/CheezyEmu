// SPDX-FileCopyrightText: Copyright 2026 CheezyEmu contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/logging.h"
#include "video_core/vulkan_common/vulkan_pipeline_binary_cache.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Vulkan::PipelineBinaryCache {
namespace {

// File: magic[8] u32 global_key_size u8 global_key[32], then records:
//   u32 record_magic u32 key_size u8 key[32] u64 payload_size payload
// Payload: u32 count, then per binary: u32 key_size u8 key[32] u64 data_size data
constexpr std::array<char, 8> FILE_MAGIC{'L', 'C', 'H', 'P', 'B', 'I', 'N', '1'};
constexpr u32 RECORD_MAGIC = 0x44524350;
constexpr size_t KEY_BYTES = VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;
constexpr size_t HEADER_BYTES = FILE_MAGIC.size() + 4 + KEY_BYTES;
constexpr size_t RECORD_HEADER_BYTES = 4 + 4 + KEY_BYTES + 8;
constexpr u32 MAX_BINARIES_PER_PIPELINE = 64;

struct Stats {
    std::atomic<u64> loaded{}, compiled{}, saved{}, load_failed{}, skipped{};
    std::atomic<u64> loaded_us{}, compiled_us{};
};

/// Reads the file at given offsets, from any thread, while it is being appended to.
class FileReader {
public:
    FileReader() = default;
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;
    ~FileReader() {
        Close();
    }

    bool Open(const std::filesystem::path& path) {
        Close();
#ifdef _WIN32
        handle = CreateFileW(path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
                             nullptr);
        return handle != INVALID_HANDLE_VALUE;
#else
        fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        return fd >= 0;
#endif
    }

    void Close() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
#else
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
#endif
    }

    [[nodiscard]] u64 Size() const {
#ifdef _WIN32
        LARGE_INTEGER size{};
        return handle != INVALID_HANDLE_VALUE && GetFileSizeEx(handle, &size)
                   ? static_cast<u64>(size.QuadPart)
                   : 0;
#else
        struct stat st {};
        return fd >= 0 && fstat(fd, &st) == 0 ? static_cast<u64>(st.st_size) : 0;
#endif
    }

    [[nodiscard]] bool Read(u64 offset, void* out, size_t size) const {
        u8* dst = static_cast<u8*>(out);
        while (size > 0) {
            const size_t want = size < (size_t{1} << 30) ? size : (size_t{1} << 30);
#ifdef _WIN32
            if (handle == INVALID_HANDLE_VALUE) {
                return false;
            }
            // With an offset a synchronous handle reads there, whatever other threads read.
            OVERLAPPED at{};
            at.Offset = static_cast<DWORD>(offset);
            at.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD got = 0;
            if (!ReadFile(handle, dst, static_cast<DWORD>(want), &got, &at) || got == 0) {
                return false;
            }
#else
            if (fd < 0) {
                return false;
            }
            const ssize_t got = pread(fd, dst, want, static_cast<off_t>(offset));
            if (got <= 0) {
                return false;
            }
#endif
            dst += got;
            offset += static_cast<u64>(got);
            size -= static_cast<size_t>(got);
        }
        return true;
    }

private:
#ifdef _WIN32
    HANDLE handle{INVALID_HANDLE_VALUE};
#else
    int fd{-1};
#endif
};

class Store {
public:
    bool Open(VkDevice device_, const vk::DeviceDispatch& dld_, const std::filesystem::path& file) {
        device = device_;
        dld = &dld_;
        path = file;
        VkPipelineBinaryKeyKHR global{.sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR};
        if (dld->vkGetPipelineKeyKHR(device, nullptr, &global) != VK_SUCCESS ||
            global.keySize == 0 || global.keySize > KEY_BYTES) {
            LOG_WARNING(Render_Vulkan, "Pipeline binaries: the driver gave no global key");
            return false;
        }
        std::array<u8, HEADER_BYTES> header{};
        std::memcpy(header.data(), FILE_MAGIC.data(), FILE_MAGIC.size());
        std::memcpy(header.data() + 8, &global.keySize, 4);
        std::memcpy(header.data() + 12, global.key, global.keySize);

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        // The file stays on disk: only where each pipeline's binaries are is kept, and they are
        // read when the pipeline is built. Held in memory whole it was 1 to 2 GB for the session.
        u64 file_size = 0;
        std::array<u8, HEADER_BYTES> found{};
        bool same_driver = false;
        if (reader.Open(path)) {
            file_size = reader.Size();
            same_driver = file_size >= HEADER_BYTES &&
                          reader.Read(0, found.data(), found.size()) && found == header;
        }
        if (same_driver) {
            u64 p = HEADER_BYTES;
            file_end = p;
            std::array<u8, RECORD_HEADER_BYTES> record{};
            while (p + RECORD_HEADER_BYTES <= file_size &&
                   reader.Read(p, record.data(), record.size())) {
                u32 magic, key_size;
                u64 len;
                std::memcpy(&magic, &record[0], 4);
                std::memcpy(&key_size, &record[4], 4);
                std::memcpy(&len, &record[8 + KEY_BYTES], 8);
                if (magic != RECORD_MAGIC || key_size == 0 || key_size > KEY_BYTES ||
                    len > file_size - p - RECORD_HEADER_BYTES) {
                    break;
                }
                index.emplace(std::string(reinterpret_cast<const char*>(&record[8]), key_size),
                              Blob{p + RECORD_HEADER_BYTES, static_cast<size_t>(len)});
                p += RECORD_HEADER_BYTES + len;
                file_end = p;
            }
            if (file_end < file_size) {
                // A session that ended mid-write leaves a partial record; drop it.
                reader.Close();
                std::filesystem::resize_file(path, file_end, ec);
                if (ec) {
                    LOG_ERROR(Render_Vulkan, "Pipeline binaries: can't trim {}", path.string());
                    return false;
                }
            }
            out.open(path, std::ios::binary | std::ios::app);
        } else {
            // First run, or the driver changed: binaries from another driver can't be used, so the
            // file is replaced rather than kept beside the new one.
            if (file_size != 0) {
                LOG_INFO(Render_Vulkan, "Pipeline binaries: driver changed, starting over");
            }
            reader.Close();
            out.open(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(header.data()), header.size());
            out.flush();
            file_end = HEADER_BYTES;
        }
        if (!out) {
            LOG_ERROR(Render_Vulkan, "Pipeline binaries: can't write {}", path.string());
            return false;
        }
        if (!reader.Open(path)) {
            LOG_ERROR(Render_Vulkan, "Pipeline binaries: can't read {}", path.string());
            return false;
        }
        LOG_INFO(Render_Vulkan, "Pipeline binaries: {} saved pipelines ({:.1f} MiB) in {}",
                 index.size(), static_cast<double>(file_end) / (1024.0 * 1024.0), path.string());
        return true;
    }

    void Close() {
        std::unique_lock lock{mutex};
        if (out.is_open()) {
            out.close();
        }
        reader.Close();
        LOG_INFO(Render_Vulkan,
                 "Pipeline binaries: {} pipelines built from saved binaries (avg {:.2f} ms), {} "
                 "compiled (avg {:.2f} ms), {} saved, {} saved binaries rejected",
                 stats.loaded.load(), Average(stats.loaded_us, stats.loaded),
                 stats.compiled.load(), Average(stats.compiled_us, stats.compiled),
                 stats.saved.load(), stats.load_failed.load());
    }

    template <typename CreateInfo, typename CreateFn>
    bool Create(const CreateInfo& ci, VkPipelineCache cache, VkPipeline* out_pipeline,
                VkResult* result, CreateFn&& create) {
        if (!Eligible(ci)) {
            ++stats.skipped;
            return false;
        }
        const auto start = std::chrono::steady_clock::now();
        const auto elapsed_us = [&] {
            return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::steady_clock::now() - start)
                                        .count());
        };
        const VkPipelineCreateInfoKHR key_info{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_INFO_KHR,
            .pNext = const_cast<CreateInfo*>(&ci),
        };
        VkPipelineBinaryKeyKHR key{.sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR};
        if (dld->vkGetPipelineKeyKHR(device, &key_info, &key) != VK_SUCCESS || key.keySize == 0 ||
            key.keySize > KEY_BYTES) {
            ++stats.skipped;
            return false;
        }
        const std::string key_bytes(reinterpret_cast<const char*>(key.key), key.keySize);

        if (const auto blob = Find(key_bytes)) {
            if (CreateFromSaved(*blob, ci, out_pipeline, create)) {
                ++stats.loaded;
                stats.loaded_us += elapsed_us();
                *result = VK_SUCCESS;
                return true;
            }
            if (stats.load_failed++ < 5) {
                LOG_WARNING(Render_Vulkan, "Pipeline binaries: a saved pipeline was rejected, "
                                           "compiling it again");
            }
        }

        // Compile with capture, then keep the driver's binaries.
        CreateInfo capture_ci = ci;
        VkPipelineCreateFlags2CreateInfoKHR flags2{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR,
            .pNext = ci.pNext,
            .flags = static_cast<VkPipelineCreateFlags2KHR>(ci.flags) |
                     VK_PIPELINE_CREATE_2_CAPTURE_DATA_BIT_KHR,
        };
        capture_ci.pNext = &flags2;
        VkResult res = create(capture_ci, cache, out_pipeline);
        if (res != VK_SUCCESS) {
            if (res < 0) {
                // Let the caller build it without capture and report its own errors.
                *out_pipeline = VK_NULL_HANDLE;
                return false;
            }
            *result = res;
            return true;
        }
        ++stats.compiled;
        Save(key_bytes, *out_pipeline);
        const VkReleaseCapturedPipelineDataInfoKHR release{
            .sType = VK_STRUCTURE_TYPE_RELEASE_CAPTURED_PIPELINE_DATA_INFO_KHR,
            .pipeline = *out_pipeline,
        };
        dld->vkReleaseCapturedPipelineDataKHR(device, &release, nullptr);
        stats.compiled_us += elapsed_us();
        *result = VK_SUCCESS;
        return true;
    }

private:
    /// Where a pipeline's binaries are in the file.
    struct Blob {
        u64 offset;
        size_t size;
    };

    static double Average(const std::atomic<u64>& us, const std::atomic<u64>& count) {
        const u64 n = count.load();
        return n == 0 ? 0.0 : static_cast<double>(us.load()) / 1000.0 / static_cast<double>(n);
    }

    static const VkBaseInStructure* Find(const void* next, VkStructureType type) {
        for (auto* s = static_cast<const VkBaseInStructure*>(next); s; s = s->pNext) {
            if (s->sType == type) {
                return s;
            }
        }
        return nullptr;
    }

    template <typename CreateInfo>
    static bool Eligible(const CreateInfo& ci) {
        // Libraries, pipelines already built from binaries and callers that pass their own
        // VkPipelineCreateFlags2 are left alone.
        return (ci.flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) == 0 &&
               !Find(ci.pNext, VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR) &&
               !Find(ci.pNext, VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT) &&
               !Find(ci.pNext, VK_STRUCTURE_TYPE_PIPELINE_BINARY_INFO_KHR) &&
               !Find(ci.pNext, VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO_KHR);
    }

    std::optional<Blob> Find(const std::string& key) {
        std::shared_lock lock{mutex};
        const auto it = index.find(key);
        return it == index.end() ? std::nullopt : std::optional<Blob>{it->second};
    }

    template <typename CreateInfo, typename CreateFn>
    bool CreateFromSaved(const Blob& where, const CreateInfo& ci, VkPipeline* out_pipeline,
                         CreateFn& create) {
        if (where.size < 4) {
            return false;
        }
        std::vector<u8> bytes(where.size);
        if (!reader.Read(where.offset, bytes.data(), bytes.size())) {
            return false;
        }
        const struct {
            const u8* data;
            size_t size;
        } blob{bytes.data(), bytes.size()};
        u32 count;
        std::memcpy(&count, blob.data, 4);
        if (count == 0 || count > MAX_BINARIES_PER_PIPELINE) {
            return false;
        }
        std::vector<VkPipelineBinaryKeyKHR> keys(count);
        std::vector<VkPipelineBinaryDataKHR> datas(count);
        size_t o = 4;
        for (u32 i = 0; i < count; ++i) {
            if (o + 4 + KEY_BYTES + 8 > blob.size) {
                return false;
            }
            keys[i] = {.sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR};
            std::memcpy(&keys[i].keySize, blob.data + o, 4);
            if (keys[i].keySize > KEY_BYTES) {
                return false;
            }
            std::memcpy(keys[i].key, blob.data + o + 4, KEY_BYTES);
            u64 size;
            std::memcpy(&size, blob.data + o + 4 + KEY_BYTES, 8);
            o += 4 + KEY_BYTES + 8;
            if (size > blob.size - o) {
                return false;
            }
            datas[i] = {.dataSize = static_cast<size_t>(size),
                        .pData = const_cast<u8*>(blob.data + o)};
            o += static_cast<size_t>(size);
        }
        const VkPipelineBinaryKeysAndDataKHR keys_and_data{
            .binaryCount = count,
            .pPipelineBinaryKeys = keys.data(),
            .pPipelineBinaryData = datas.data(),
        };
        const VkPipelineBinaryCreateInfoKHR binary_ci{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR,
            .pKeysAndDataInfo = &keys_and_data,
        };
        std::vector<VkPipelineBinaryKHR> binaries(count, VK_NULL_HANDLE);
        VkPipelineBinaryHandlesInfoKHR handles{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR,
            .pipelineBinaryCount = count,
            .pPipelineBinaries = binaries.data(),
        };
        VkResult res = dld->vkCreatePipelineBinariesKHR(device, &binary_ci, nullptr, &handles);
        if (res == VK_SUCCESS) {
            const VkPipelineBinaryInfoKHR binary_info{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_INFO_KHR,
                .pNext = ci.pNext,
                .binaryCount = count,
                .pPipelineBinaries = binaries.data(),
            };
            CreateInfo from_binaries = ci;
            from_binaries.pNext = &binary_info;
            // A pipeline built from binaries takes no pipeline cache.
            res = create(from_binaries, VK_NULL_HANDLE, out_pipeline);
        }
        for (const VkPipelineBinaryKHR binary : binaries) {
            if (binary != VK_NULL_HANDLE) {
                dld->vkDestroyPipelineBinaryKHR(device, binary, nullptr);
            }
        }
        if (res != VK_SUCCESS) {
            *out_pipeline = VK_NULL_HANDLE;
            return false;
        }
        return true;
    }

    void Save(const std::string& key, VkPipeline pipeline) {
        const VkPipelineBinaryCreateInfoKHR binary_ci{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR,
            .pipeline = pipeline,
        };
        VkPipelineBinaryHandlesInfoKHR handles{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR,
        };
        if (dld->vkCreatePipelineBinariesKHR(device, &binary_ci, nullptr, &handles) !=
                VK_SUCCESS ||
            handles.pipelineBinaryCount == 0 ||
            handles.pipelineBinaryCount > MAX_BINARIES_PER_PIPELINE) {
            return;
        }
        std::vector<VkPipelineBinaryKHR> binaries(handles.pipelineBinaryCount, VK_NULL_HANDLE);
        handles.pPipelineBinaries = binaries.data();
        if (dld->vkCreatePipelineBinariesKHR(device, &binary_ci, nullptr, &handles) !=
            VK_SUCCESS) {
            return;
        }
        std::vector<u8> payload(4);
        const u32 count = static_cast<u32>(binaries.size());
        std::memcpy(payload.data(), &count, 4);
        bool ok = true;
        for (const VkPipelineBinaryKHR binary : binaries) {
            const VkPipelineBinaryDataInfoKHR data_info{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_DATA_INFO_KHR,
                .pipelineBinary = binary,
            };
            VkPipelineBinaryKeyKHR binary_key{.sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR};
            size_t size = 0;
            if (dld->vkGetPipelineBinaryDataKHR(device, &data_info, &binary_key, &size, nullptr) !=
                    VK_SUCCESS ||
                size == 0) {
                ok = false;
                break;
            }
            const size_t o = payload.size();
            payload.resize(o + 4 + KEY_BYTES + 8 + size);
            std::memcpy(&payload[o], &binary_key.keySize, 4);
            std::memcpy(&payload[o + 4], binary_key.key, KEY_BYTES);
            const u64 size64 = size;
            std::memcpy(&payload[o + 4 + KEY_BYTES], &size64, 8);
            if (dld->vkGetPipelineBinaryDataKHR(device, &data_info, &binary_key, &size,
                                                &payload[o + 4 + KEY_BYTES + 8]) != VK_SUCCESS) {
                ok = false;
                break;
            }
        }
        for (const VkPipelineBinaryKHR binary : binaries) {
            dld->vkDestroyPipelineBinaryKHR(device, binary, nullptr);
        }
        if (!ok) {
            return;
        }
        std::unique_lock lock{mutex};
        if (index.contains(key)) {
            return;
        }
        if (!out.is_open()) {
            return;
        }
        const u32 magic = RECORD_MAGIC;
        const u32 key_size = static_cast<u32>(key.size());
        std::array<u8, KEY_BYTES> key_padded{};
        std::memcpy(key_padded.data(), key.data(), key.size());
        const u64 len = payload.size();
        out.write(reinterpret_cast<const char*>(&magic), 4);
        out.write(reinterpret_cast<const char*>(&key_size), 4);
        out.write(reinterpret_cast<const char*>(key_padded.data()), KEY_BYTES);
        out.write(reinterpret_cast<const char*>(&len), 8);
        out.write(reinterpret_cast<const char*>(payload.data()),
                  static_cast<std::streamsize>(payload.size()));
        out.flush();
        if (!out) {
            // Disk full, typically. What is in the file past here can't be trusted: stop saving;
            // the next start drops the partial record.
            LOG_ERROR(Render_Vulkan, "Pipeline binaries: writing {} failed, no more are saved "
                                     "this session",
                      path.string());
            out.close();
            return;
        }
        // Flushed, so a later build of the same pipeline can read it back from the file.
        index.emplace(key, Blob{file_end + RECORD_HEADER_BYTES, payload.size()});
        file_end += RECORD_HEADER_BYTES + len;
        ++stats.saved;
    }

    VkDevice device{};
    const vk::DeviceDispatch* dld{};
    std::filesystem::path path;
    std::ofstream out;
    FileReader reader;
    u64 file_end{};
    std::unordered_map<std::string, Blob> index;
    std::shared_mutex mutex;
    Stats stats;
};

std::shared_mutex active_mutex;
std::unique_ptr<Store> active;

} // Anonymous namespace

void Activate(VkDevice device, const vk::DeviceDispatch& dld, const std::filesystem::path& file) {
    if (!dld.vkGetPipelineKeyKHR || !dld.vkCreatePipelineBinariesKHR ||
        !dld.vkDestroyPipelineBinaryKHR || !dld.vkGetPipelineBinaryDataKHR ||
        !dld.vkReleaseCapturedPipelineDataKHR) {
        return;
    }
    auto store = std::make_unique<Store>();
    if (!store->Open(device, dld, file)) {
        return;
    }
    std::unique_lock lock{active_mutex};
    if (active) {
        active->Close();
    }
    active = std::move(store);
}

void Deactivate() {
    std::unique_lock lock{active_mutex};
    if (active) {
        active->Close();
        active.reset();
    }
}

bool CreateGraphicsPipeline(VkDevice device, const vk::DeviceDispatch& dld,
                            const VkGraphicsPipelineCreateInfo& ci, VkPipelineCache cache,
                            VkPipeline* out, VkResult* result) {
    std::shared_lock lock{active_mutex};
    if (!active) {
        return false;
    }
    return active->Create(ci, cache, out, result,
                          [&](const VkGraphicsPipelineCreateInfo& info, VkPipelineCache c,
                              VkPipeline* p) {
                              return dld.vkCreateGraphicsPipelines(device, c, 1, &info, nullptr,
                                                                   p);
                          });
}

bool CreateComputePipeline(VkDevice device, const vk::DeviceDispatch& dld,
                           const VkComputePipelineCreateInfo& ci, VkPipelineCache cache,
                           VkPipeline* out, VkResult* result) {
    std::shared_lock lock{active_mutex};
    if (!active) {
        return false;
    }
    return active->Create(ci, cache, out, result,
                          [&](const VkComputePipelineCreateInfo& info, VkPipelineCache c,
                              VkPipeline* p) {
                              return dld.vkCreateComputePipelines(device, c, 1, &info, nullptr,
                                                                  p);
                          });
}

} // namespace Vulkan::PipelineBinaryCache
