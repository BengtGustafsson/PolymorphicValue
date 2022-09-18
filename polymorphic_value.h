/*

Test implementation of a polymorphic_value class for possible introduction into the C++ standard library.

This software is provided under the MIT license:

Copyright 2022 Bengt Gustafsson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,
merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Further information at: https://opensource.org/licenses/MIT.

*/



#pragma once

#include <memory>
#include <type_traits>
#include <stdexcept>
#include <utility>
#include <optional>


#if IS_STANDARDIZED

#define STD std
namespace std {

#else

#define STD stdx
namespace stdx {

using namespace std;

#endif


template<typename T, size_t SZ = 64> class polymorphic_value {
public:
    const static size_t sbo_size = SZ >= sizeof(T) ? SZ : 1;
    polymorphic_value() {}
    polymorphic_value(nullopt_t) {}
    polymorphic_value(const polymorphic_value& src) requires is_copy_constructible_v<T> {
        src.m_handler.imbue_handler(m_handler);
        src.m_handler.copy(m_data, src.m_data);
    }
    polymorphic_value(polymorphic_value&& src) requires is_move_constructible_v<T> {
        src.m_handler.imbue_handler(m_handler);
        src.m_handler.move(m_data, src.m_data);
        src.reset();
    }
    template<typename U, typename... Args> polymorphic_value(in_place_type_t<U>, Args&&... args)  requires is_base_of_v<T, U> {
        emplace<U>(forward<Args>(args)...);
    }

    ~polymorphic_value() {
        m_handler.destroy(m_data);
    }

    // TODO: To handle the odd case that a T is copy-assignable but not copy-constructible or vice versa
    // handler would actually need to have both copy_assign and copy_contruct. Same for move. 
    // Note that this can only be done if the embedded U of both me and src are the same, otherwise this destroy/construct sequence must be done anyway.
    polymorphic_value& operator=(const polymorphic_value& src) requires is_copy_constructible_v<T> {
        if (this == &src)
            return *this;

        m_handler.destroy(m_data);
        src.m_handler.imbue_handler(m_handler);
        src.m_handler.copy(m_data, src.m_data);
        return *this;
    };

    polymorphic_value& operator=(polymorphic_value&& src) requires is_move_constructible_v<T> {
        if (this == &src)
            return *this;

        m_handler.destroy(m_data);
        src.m_handler.imbue_handler(m_handler);
        src.m_handler.move(m_data, src.m_data);
        src.reset();
        return *this;
    };

    // Create object of subclass U of T, or by default a T.
    template<typename U = T, typename... Args> void emplace(Args&&... args) {
        m_handler.destroy(m_data);
        if constexpr (sizeof(U) <= sbo_size) {
            new(&m_handler) small_handler<U>;
            construct_at(reinterpret_cast<U*>(m_data.m_bytes), forward<Args>(args)...);
        }
        else {
            new(&m_handler) big_handler<U>;
            construct_at(&m_data.m_ptr, make_unique<U>(forward<Args>(args)...));
        }
    }

    // Get rid of a stored object, resetting the handler so that no double delete occurs later and so that operator bool returns false.
    void reset() { m_handler.destroy(m_data); new(&m_handler) handler_base; }

    operator bool() const { return get() != nullptr; }

    // Access stored object. This enabled the pattern dynamic_cast<U*>(v.get()) to check and access the data.
    // Without it
    //    v.has_value<U>() ? static_cast<U*>(&*v) : nullptr
    // would have to be used which seems too ugly. The more classical:
    //     dynamic_cast<U*>(&*v);
    // does not work without UB as operator* would return "struct at null" but maybe that's not a big deal.
    //
    // I would rather add a get() to optional, except that it is less useful there as there is only one null condition so maybe not.
    //
    T* get() { return m_handler.get(m_data); }
    const T* get() const { return m_handler.get(m_data); }

    T& operator*() { return *get(); }
    const T& operator*() const { return *get(); }

    T* operator->() { return get(); }
    const T* operator->() const { return get(); }

    // optional API
    // Maybe a holds_alternative<U> from variant is more appropriate? But viewing different subclasses as alternatives seems a bit
    // misleading.
    bool has_value() const { return *this; }
    template<typename U> bool has_value() const { return dynamic_cast<const U*>(get()) != nullptr; }

    // Note: Skip rvalue versions for now.
    template<typename U = T> U& value() {
        U* ret = dynamic_cast<U*>(get());
        if (ret == nullptr)
            throw bad_optional_access();
        return *ret;
    }
    template<typename U = T> const U& value() const {
        U* ret = dynamic_cast<const U*>(get());
        if (ret == nullptr)
            throw bad_optional_access();
        return *ret;
    }
    template<typename U = T, typename V> U value_or(V&& default_value) const {
        const U* ret = dynamic_cast<const U*>(get());
        if (ret != nullptr)
            return *ret;
        else
            return static_cast<U>(forward<V>(default_value));
    }

    // Note: and_then requires F to return optional<X> or polymorphic_value<X> for some type X.
    // This requirement is from optional, but in fact is quite strange, it could be any return type with a value-constructed
    // default, such as a pointer.
    template<typename U = T, typename F> auto and_then(F&& f) {
        using R = remove_cvref_t<invoke_result_t<F, U&>>; // R must be some optional type.
        if (has_value<U>())
            return f(*static_cast<U*>(get()));
        else
            return R();
    }
    template<typename U = T, typename F> auto and_then(F&& f) const {
        using R = remove_cvref_t<invoke_result_t<F, const U&>>;
        if (has_value<U>())
            return f(*static_cast<const U*>(get()));
        else
            return R();
    }

    // transform wraps the return value of F in an optional.
    template<typename U = T, typename F> auto transform(F&& f) {
        using R = remove_cvref_t<invoke_result_t<F, U&>>;
        if (has_value<U>())
            return optional<R>(f(*static_cast<U*>(get())));
        else
            return optional<R>();
    }
    template<typename U = T, typename F> auto transform(F&& f) const {
        using R = remove_cvref_t<invoke_result_t<F, const U&>>;
        if (has_value<U>())
            return optional<R>(f(*static_cast<const U*>(get())));
        else
            return optional<R>();
    }

    // or_else requires F to return some optional.
    template<typename U = T, typename F> auto or_else(F&& f) {
        using R = remove_cvref_t<invoke_result_t<F>>;
        if (has_value<U>())
            return R(*static_cast<U*>(get()));
        else
            return f();
    }
    template<typename U = T, typename F> auto or_else(F&& f) const {
        using R = remove_cvref_t<invoke_result_t<F>>;
        if (has_value<U>())
            return R(*static_cast<const U*>(get()));
        else
            return f();
    }

private:
    union data {
        data() : m_ptr(nullptr) {}
        ~data() {}

        byte m_bytes[sbo_size];
        unique_ptr<T> m_ptr;
    };

    // Note: handler_base is not abstract, instead it is used for empty objects.
    struct handler_base {
        virtual void imbue_handler(handler_base& dest) const { new(&dest) handler_base; }

        virtual T* get(data& d) const { return nullptr; }
        virtual const T* get(const data& d) const { return nullptr; }

        virtual void copy(data& dest, const data& src) const {}
        virtual void move(data& dest, data& src) const {}
        virtual void destroy(data& d) const {}
    };
    
    template<typename U> struct small_handler final : public handler_base {
        void imbue_handler(handler_base& dest) const override { new(&dest) small_handler<U>; }

        T* get(data& d) const override { return static_cast<T*>(reinterpret_cast<U*>(d.m_bytes)); }
        const T* get(const data& d) const override { return static_cast<const T*>(reinterpret_cast<const U*>(d.m_bytes)); }

        void copy(data& dest, const data& src) const override {
            if constexpr (is_copy_constructible_v<U>)
                construct_at<U>(reinterpret_cast<U*>(dest.m_bytes), *reinterpret_cast<const U*>(src.m_bytes));
            else
                throw runtime_error("Tried to copy an object of move only subclass in a polymorphic_value<T> where T is copyable");
        }
        
        void move(data& dest, data& src) const override {
            if constexpr (is_move_constructible_v<U>)
                construct_at<U>(reinterpret_cast<U*>(dest.m_bytes), std::move(*reinterpret_cast<U*>(src.m_bytes)));
            else
                throw runtime_error("Tried to move an object of immovable subclass in a polymorphic_value<T> where T is movable");
        }

        void destroy(data& d) const override { destroy_at(reinterpret_cast<U*>(d.m_bytes)); }
    };
    
    template<typename U> struct big_handler final : public handler_base {
        void imbue_handler(handler_base& dest) const override { new(&dest) big_handler<U>; }

        T* get(data& d) const override { return d.m_ptr.get(); }
        const T* get(const data& d) const override { return d.m_ptr.get(); }

        void copy(data& dest, const data& src) const override { 
            if constexpr (is_copy_constructible_v<U>)
                construct_at(&dest.m_ptr, make_unique<U>(static_cast<const U&>(*src.m_ptr)));
            else
                throw runtime_error("Tried to copy an object of move only subclass in a polymorphic_value<T> where T is copyable");
        }
        void move(data& dest, data& src) const override {
            if constexpr (is_move_constructible_v<U>)
                construct_at(&dest.m_ptr, std::move(src.m_ptr));
            else
                throw runtime_error("Tried to move an object of immovable subclass in a polymorphic_value<T> where T is movable");
        }

        void destroy(data& d) const override { d.m_ptr = nullptr; }
    };

    handler_base m_handler;
    data m_data;
};

template<typename T, typename U = T, typename... Args> polymorphic_value<T> make_polymorphic_value(Args&&... args)
{
    return polymorphic_value<T>(in_place_type<U>, forward<Args>(args)...);
}

template<typename T, size_t SZ, typename U = T, typename... Args> polymorphic_value<T, SZ> make_polymorphic_value(Args&&... args)
{
    return polymorphic_value<T, SZ>(in_place_type<U>, forward<Args>(args)...);
}

}       // Namespace std or stdx
