# PolymorphicValue

Variation of std::polymorphic_value (P0201) with SBO support and options.

In the current state the class is located in a separate namespace stdx unless the preprocessor variable IS_STANDARDIZED is set to 1,
in which case it is located in the std namespace.

## Differences from P0201

In contrast with the implementation at [https://github.com/jbcoe/polymorphic_value] as described by 
[https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0201r1.pdf] this implementation:

- Has a SBO buffer to avoid heap allocations

- Does not risk slicing at copy by not allowing construction from U* or U& as such pointers or references could
  potentially point at further subclasses. In P0201 this is a runtime check relying on dynamic_cast.

- Does not support copying between polymorphic_value<T> and polymorphic_value<W> even if T and W have a inheritance relationship

- Does not need heap storage for control blocks

- Does not support allocators (P0201 does not either, but the implementation does).

- Has options to prevent copying and even moving, even if T allows it, in case some U doesn't.

- Has an option to prevent heap allocation, in which case it is a compile error if U does not fit the SBO buffer.

- Has an option to set a higher alignment than T's and if a U has a larger alignment than set this is a compile error.

- As polymorphic_value can be empty by default construction, moved from or U constructor exception, optional<polymorphic_value> is
not optimally efficient, and also it would require double dereference to reach the stored object, with two existence checks, the new
monadic optional API is directly implemented (with some twists regarding U subclasses).

## Description

polymorphic_value is a by value container of one object of any subclass of T with SBO for objects of small enough size. The API is
close to unique_ptr. The semantics is also close but with the difference that references to the data are not preserved when the
object is moved. Another difference is that the object has copy semantics if T has. 

### Constructing polymorphic_values with subtype objects in them

There are three ways to set an instance of a subtype U in a polymorphic_value<T> that serve different purposes. When creating a
polymorphic_value as a stack variable or return value the static method make<U>(args...) is usually the most ergonomic. To contruct
a member in a member initializer list the constructor which takes a std::in_place_type<U> as its first parameter is the best choice.
When changing the value in a pre-existing polymorphic_type object the emplace<U> method is usually preferrable. When used
appropriately these different ways minimize the extra moves and copies required.


// Example classes
class MyType {
    virtual void run();
};

class MySubType : public MyType {
    void run() override;
};


// Use options to save some size and make sure that no subclass is large and spills to heap.
using MyPoly = std::polymorphic_value<MyType, { .size = sizeof(MySubType}, .heap = false}>;


// Example class containing a MyPoly value.
class User {
    User() : m_object(std::in_place_type<MySubType>) {}     // Use in_place_type in member initializer list
    User(MyPoly p) : m_object(std::move(p)) {}

    void back_to_basics() {
        m_object.emplace<MyType>();     // Use emplace to change type of m_object to MyType.

    MyPoly m_object;
};


// Use make when returning a polymorphic_value
MyPoly createPoly(bool useSub)
{
    if (useSub)
        return MyPoly::make<MySubType>());
    else
        return MyPoly::make<MyType>());
}


// Select which MyType subclass to use at runtime.
User myUser(createPoly(true));
 

### Handling that subclasses have different properties than the base class

The properties copyability, movability and alignment of polymorphic_value by default follows the properties of the template
parameter T, i.e. the base class of all possible objects that can be controlled by the polymorphic_value. However, it is possible
that some subclass U of T for instance is not copyable while T is. This by default causes a compile time error if a U is used with
polymrphic_value<T>. However, the copy behaviour of polymorphic_value can be modified using an option, so that polymorphic_value
becomes non-copyable although T is copyable. Setting this option allows U objects to be controlled by the polymorphic_value.

Similarly movability and alignment can also be controlled by options.

### Selecting a suitable SBO size

The SBO size always default to an implementation defined default but can be changed by an option. This implementation sets the SBO
size to 64 by default, but if sizeof(T) > 64 it is set to 0 to force use of heap storage always. This avoids having a SBO buffer
which is never used.

### Preventing heap allocation

In some scenarios, especially in embedded systems, heap allocations may be precluded. To be able to check against heap allocations
already at compile time there is a special option that can be set to false to prevent allocations. In this case the size of each U
being used is checked at compile time towards the set SBO size, thereby simplifying selecting the suitable SBO size.

If heap allocations are prevented by this option the SBO size is set to be at least as large as T. However, if subclasses add
members the SBO size must be adjusted manually.

### Setting options

Options are set in the second template parameter of polymorphic_value. Thanks to C++20 designated initializers this can be done with
fairly good syntax. The type of the second template parameter is polymorphic_value_options but luckily you don't need to spell this
out but can just use a initializer clause as the value:

using MyPoly = std::polymorphic_value<MyType, {.size = 32, .heap = false, .copy = false }>;


## Implementation details

The implementation of polymorphic_value is fairly straight-forward. The data area consists of a union of a unique_ptr<T> and a byte
buffer of the set SBO size and alignment. To control all behaviour there is a nested class hierarchy of classes with no members, only a vtable
pointer. This makes all these classes the same size (one pointer) and the appropriate subclass is in place constructed in the member
called m_handler whic is of the baseclass type. The baseclass implements methods for an empty polymorphic_value object.

The embedded handler object is responsible for copying itself as well as the data depending on if SBO is in effect or not. There are
some optimizations available which are not employed:

- if assigning between equally typed contained objects the copy/move assignment could be called instead of copy/move
construct + destroy. However, for types that don't do the rule of 5 right this would require additional Options which is too boring.

## Notes and limitations

Note that it is not possible to assign or construct from a polymorphic_value<U> to a polymorphic_value<T> even if U is a subclass of
T. This limitation is due to the complexities of handling the this pointer offsets in case the polymorphic_value<U> actually
contains an object of a further subclass Z: While the handler imbued by polymorphic_value<U> in polymorphic_value<T> handles the
conversion from Z to U correctly the further conversion from U to T can't be handled without extra unbounded data. Some limited
support such as "only without multiple inheritance" or "only without virtual inheritance" could be added but it would have size and
speed cost.

Note that it is also not possible to assign between polymorphic_values with different SZ values as this would potentially require
copying the data from an external to internal storage or vice versa. As the handler of the source imbues itself on the destination
this fails if the destination has a different SZ. Workarounds were tried but were not successful without incurring at least an extra
virtual call and an if when accessing the underlying data, which seems too costly for such a rarely usable feature.

The virtual methods copy and move of the nested handler class could be made optional dependig on the move/copy semantics but
unfortunately requires clauses are not allowed on virtual methods (when could that ever be useful?) so it is complicated to get just
to save a few bytes of unused vtable entries. Fixing this is a QoI issue which can be done by real implementations.
