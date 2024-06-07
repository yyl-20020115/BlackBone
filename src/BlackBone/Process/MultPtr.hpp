#pragma once
#include "Process.h"
#include "../Misc/Trace.hpp"

#include <stdint.h>
#include <type_traits>
#include <excpt.h>

namespace blackbone
{

/// <summary>
/// Multi-level pointer wrapper for local process
/// </summary>
template<typename T>
class multi_ptr
{
public:
    //using type = std::remove_pointer_t<T>;
    //using type_ptr = std::add_pointer_t<type>;
    //using vecOffsets = std::vector<intptr_t>;

    constexpr static bool type_is_ptr = std::is_pointer_v<T>;
protected:
    uintptr_t _base = 0;
    std::add_pointer_t<std::remove_pointer_t<T>> _ptr = nullptr;
    std::vector<intptr_t> _offsets;

public:
    multi_ptr() = default;

    multi_ptr( uintptr_t base, const std::vector<intptr_t>& offsets = std::vector<intptr_t>() )
        : _base( base )
        , _offsets( offsets ) { }

    /// <summary>
    /// Object getters
    /// </summary>
    /// <returns>Object pointer, if valid</returns>
    inline std::add_pointer_t<std::remove_pointer_t<T>> get()         { return read(); }
    inline operator std::add_pointer_t<std::remove_pointer_t<T>>()    { return read(); }
    inline std::add_pointer_t<std::remove_pointer_t<T>> operator ->() { return read(); }

protected:
    /// <summary>
    /// Get object pointer from base and offsets
    /// </summary>
    /// <returns>Object ptr, if valid</returns>
    virtual std::add_pointer_t<std::remove_pointer_t<T>> read()
    {
        intptr_t i = -1;
        uintptr_t ptr = _base;

        __try
        {
            if (!ptr)
                return _ptr = nullptr;

            ptr = *reinterpret_cast<intptr_t*>(ptr);
            if (!_offsets.empty())
            {
                for (i = 0; i < static_cast<intptr_t>(_offsets.size()) - 1; i++)
                    ptr = *reinterpret_cast<uintptr_t*>(ptr + _offsets[i]);

                ptr += _offsets.back();
                if (type_is_ptr)
                    ptr = *reinterpret_cast<uintptr_t*>(ptr);
            }

            return _ptr = reinterpret_cast<std::add_pointer_t<std::remove_pointer_t<T>>>(ptr);
        }
        // Invalid address
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION)
        {
            BLACKBONE_TRACE(
                "Invalid pointer derefrence: base 0x%p, offset 0x%08x, target address 0x%p",
                _base, (i > 0 ? _offsets[i] : -1), ptr
            );

            return _ptr = nullptr;
        }
    }

};

/// <summary>
/// Multi-level pointer wrapper for remote process
/// </summary>
template<typename T>
class multi_ptr_ex : public multi_ptr<T>
{
public:
    /// <summary>
    /// Initializes a new instance of the <see cref="multi_ptr_ex"/> class.
    /// </summary>
    /// <param name="proc">Target process</param>
    /// <param name="base">Base address</param>
    /// <param name="offsets">Offsets</param>
    multi_ptr_ex( Process* proc, uintptr_t base = 0, const std::vector<intptr_t>& offsets = std::vector<intptr_t>() )
        : _proc( proc )
        , multi_ptr<T>( base, offsets ) { }

    /// <summary>
    /// Commit changed object into process
    /// </summary>
    /// <returns>Status code</returns>
    NTSTATUS commit()
    {
        auto ptr = get_ptr();
        if (ptr == 0)
            return STATUS_ACCESS_VIOLATION;

        return _proc->memory().Write( ptr, sizeof( _data ), &_data );
    }

private:
    /// <summary>
    /// Read object from pointer
    /// </summary>
    /// <returns>Pointer to local copy or nullptr if invalid</returns>
    virtual std::add_pointer_t<std::remove_pointer_t<T>> read()
    { 
        auto ptr = get_ptr();
        if (ptr == 0)
            return nullptr;

        return NT_SUCCESS( _proc->memory().Read( ptr, sizeof( _data ), &_data ) ) ? &_data : nullptr;
    }
    
    /// <summary>
    /// Get target pointer
    /// </summary>
    /// <returns>Pointer value or 0 if chain is invalid</returns>
    uintptr_t get_ptr()
    {
        uintptr_t ptr = __super::_base;
        if (!NT_SUCCESS( _proc->memory().Read( ptr, ptr ) ))
            return 0;

        if (!__super::_offsets.empty())
        {
            for (intptr_t i = 0; i < static_cast<intptr_t>(__super::_offsets.size()) - 1; i++)
                if (!NT_SUCCESS( _proc->memory().Read( ptr + __super::_offsets[i], ptr ) ))
                    return 0;

            ptr += __super::_offsets.back();
            if (__super::type_is_ptr)
                if (!NT_SUCCESS( _proc->memory().Read( ptr, ptr ) ))
                    return 0;
        }

        return ptr;
    }


private:
    Process* _proc = nullptr;       // Target process
    std::remove_pointer_t<T> _data;                     // Local object copy
};
}