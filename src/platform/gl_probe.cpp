#include "platform/gl_probe.h"

#include <cstdint>

// Punteros de glad que rlgl usa en OpenGL 3.3. Están definidos en C dentro de raylib.lib
// (rcore.c incluye glad.h con GLAD_GL_IMPLEMENTATION); aquí se declaran con sus tipos de GL
// sin incluir glad.h, que no está entre los includes públicos de raylib.
extern "C" {
using GlDrawArrays = void(__stdcall*)(unsigned int mode, int first, int count);
using GlDrawElements = void(__stdcall*)(unsigned int mode, int count, unsigned int type,
                                        const void* indices);
using GlDrawArraysInstanced = void(__stdcall*)(unsigned int mode, int first, int count,
                                               int instances);
using GlDrawElementsInstanced = void(__stdcall*)(unsigned int mode, int count, unsigned int type,
                                                 const void* indices, int instances);
using GlGenQueries = void(__stdcall*)(int n, unsigned int* ids);
using GlDeleteQueries = void(__stdcall*)(int n, const unsigned int* ids);
using GlBeginQuery = void(__stdcall*)(unsigned int target, unsigned int id);
using GlEndQuery = void(__stdcall*)(unsigned int target);
using GlGetQueryObjectiv = void(__stdcall*)(unsigned int id, unsigned int name, int* value);
using GlGetQueryObjectui64v = void(__stdcall*)(unsigned int id, unsigned int name,
                                               std::uint64_t* value);
using GlGetString = const unsigned char*(__stdcall*)(unsigned int name);

extern GlDrawArrays glad_glDrawArrays;
extern GlDrawElements glad_glDrawElements;
extern GlDrawArraysInstanced glad_glDrawArraysInstanced;
extern GlDrawElementsInstanced glad_glDrawElementsInstanced;
extern GlGenQueries glad_glGenQueries;
extern GlDeleteQueries glad_glDeleteQueries;
extern GlBeginQuery glad_glBeginQuery;
extern GlEndQuery glad_glEndQuery;
extern GlGetQueryObjectiv glad_glGetQueryObjectiv;
extern GlGetQueryObjectui64v glad_glGetQueryObjectui64v;
extern GlGetString glad_glGetString;
}

namespace rally::platform {

namespace {

constexpr unsigned int kGlTimeElapsed = 0x88BF;
constexpr unsigned int kGlQueryResult = 0x8866;
constexpr unsigned int kGlQueryResultAvailable = 0x8867;
constexpr unsigned int kGlRenderer = 0x1F01;

// Originales de glad y cuentas. Un solo hilo dibuja: sin atómicos.
GlDrawArrays original_draw_arrays = nullptr;
GlDrawElements original_draw_elements = nullptr;
GlDrawArraysInstanced original_draw_arrays_instanced = nullptr;
GlDrawElementsInstanced original_draw_elements_instanced = nullptr;
DrawCallCounts counts;

void __stdcall counted_draw_arrays(unsigned int mode, int first, int count) {
    ++counts.calls;
    original_draw_arrays(mode, first, count);
}

void __stdcall counted_draw_elements(unsigned int mode, int count, unsigned int type,
                                     const void* indices) {
    ++counts.calls;
    original_draw_elements(mode, count, type, indices);
}

void __stdcall counted_draw_arrays_instanced(unsigned int mode, int first, int count,
                                             int instances) {
    ++counts.calls;
    ++counts.instanced_calls;
    counts.instances += instances;
    original_draw_arrays_instanced(mode, first, count, instances);
}

void __stdcall counted_draw_elements_instanced(unsigned int mode, int count, unsigned int type,
                                               const void* indices, int instances) {
    ++counts.calls;
    ++counts.instanced_calls;
    counts.instances += instances;
    original_draw_elements_instanced(mode, count, type, indices, instances);
}

// Pone un envoltorio en un puntero de glad si no está ya. Guarda el original la primera vez
// y si glad lo ha vuelto a cargar.
template <typename Pointer>
bool hook(Pointer& slot, Pointer& original, Pointer wrapper) {
    if (slot == wrapper) {
        return true;
    }
    if (slot == nullptr) {
        return false;
    }
    original = slot;
    slot = wrapper;
    return true;
}

bool hook_all() {
    const bool arrays = hook(glad_glDrawArrays, original_draw_arrays, &counted_draw_arrays);
    const bool elements = hook(glad_glDrawElements, original_draw_elements, &counted_draw_elements);
    const bool arrays_instanced = hook(glad_glDrawArraysInstanced, original_draw_arrays_instanced,
                                       &counted_draw_arrays_instanced);
    const bool elements_instanced =
        hook(glad_glDrawElementsInstanced, original_draw_elements_instanced,
             &counted_draw_elements_instanced);
    return arrays && elements && arrays_instanced && elements_instanced;
}

} // namespace

DrawCallCounter::DrawCallCounter() : available_(hook_all()) {}

DrawCallCounts DrawCallCounter::take() {
    available_ = hook_all();
    const DrawCallCounts result = counts;
    counts = DrawCallCounts{};
    return result;
}

GpuTimer::GpuTimer() {
    available_ = glad_glGenQueries != nullptr && glad_glDeleteQueries != nullptr &&
                 glad_glBeginQuery != nullptr && glad_glEndQuery != nullptr &&
                 glad_glGetQueryObjectiv != nullptr && glad_glGetQueryObjectui64v != nullptr;
    if (available_) {
        glad_glGenQueries(kQueries, queries_.data());
    }
}

GpuTimer::~GpuTimer() {
    if (available_) {
        glad_glDeleteQueries(kQueries, queries_.data());
    }
}

void GpuTimer::begin() {
    if (!available_ || active_) {
        return;
    }
    const auto slot = static_cast<std::size_t>(next_);
    if (pending_[slot]) {
        // El anillo ha dado la vuelta sin que llegara el resultado: se pierde esa muestra.
        pending_[slot] = false;
        ++lost_;
        oldest_ = (next_ + 1) % kQueries;
    }
    glad_glBeginQuery(kGlTimeElapsed, queries_[slot]);
    active_ = true;
}

void GpuTimer::end() {
    if (!available_ || !active_) {
        return;
    }
    glad_glEndQuery(kGlTimeElapsed);
    pending_[static_cast<std::size_t>(next_)] = true;
    next_ = (next_ + 1) % kQueries;
    active_ = false;
}

std::int64_t GpuTimer::take_result() {
    std::int64_t result = -1;
    if (!available_) {
        return result;
    }
    // Las pendientes van seguidas desde oldest_; se leen en orden mientras estén listas.
    for (int i = 0; i < kQueries; ++i) {
        const auto slot = static_cast<std::size_t>(oldest_);
        if (!pending_[slot]) {
            break;
        }
        int ready = 0;
        glad_glGetQueryObjectiv(queries_[slot], kGlQueryResultAvailable, &ready);
        if (ready == 0) {
            break;
        }
        std::uint64_t elapsed = 0;
        glad_glGetQueryObjectui64v(queries_[slot], kGlQueryResult, &elapsed);
        result = static_cast<std::int64_t>(elapsed);
        pending_[slot] = false;
        oldest_ = (oldest_ + 1) % kQueries;
    }
    return result;
}

const char* gl_renderer() {
    const unsigned char* name =
        glad_glGetString != nullptr ? glad_glGetString(kGlRenderer) : nullptr;
    return name != nullptr ? reinterpret_cast<const char*>(name) : "desconocida";
}

} // namespace rally::platform
