# PolymorphicValue

Variation of std::polymorphic_value (P0201) with SBO support and options

## Differences

In contrast with the implementation at [https://github.com/jbcoe/polymorphic_value] as described by 
[https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0201r1.pdf] this implementation:

- Has a SBO buffer option to avoid heap allocations

- Does not risk slicing at copy by not allowing construction from U* or U& as such pointers or references could
  potentially point at further subclasses. In P0201 this is a runtime check relying on dynamic_cast.

- Does not support copying between polymorphic_value<T> and polymorphic_value<W> even if T and W have a inheritance relationship

- Does not need heap storage for control blocks

- Does not support allocators (P0201 does not either, but the implementation does).

- Has options to prevent copying and even moving, even if T allows it.

- Has an option to prevent heap allocation, in which case it is a compile error if U does not fit the SBO buffer.

- Has an option to set a higher alignment than T's and if a  has a larger alignment than set this is a compile error.

Note: Not all of these features are available in the first version.

Warning: This initial version does not take the alignment requirement of T into account, and will fail if it is larger than that of
a pointer.

## Description

A by value container of one object of any subclass of T with SBO for objects of small enough size. The API is close to unique_ptr.
The semantics is also close but with the difference that references to the data are not preserved when the object is moved. Another
difference is that the object has copy semantics if T (and U) has. Note that if T has copy semantics but U doesn't performing a copy
will cause a runtime error.

It would be beneficial to have a variant which never allocates on the heap and causes compile time error if the actual object is too
large. Due to the cast functionality it would however be possible that these errors could crop up at run time anyway.

The embedded handler object is responsible for copying itself as well as the data depending on if SBO is in effect or not. There are
some optimizations available which are not employed:

- if assigning between equally typed contained objects the copy/move assignment could be called instead of destroy + copy/move
construct.

- we don't actually need both const and non-const get virtual methods in handler, a cast is sufficient.

The static data member sbo_size is the resulting size of the SBO buffer. It is usually the same as SZ but if T itself is larger than
SZ we know that the SBO buffer would never be possible to use, so sbo_size is set to 1 which forces the "big" handlers to be set up
always and brings the size of the polymorphic_value down to two pointers. This reduces the need for manually tweaking the SZ value
for different Ts. It would be possible to let sbo_size somehow depend on sizeof(T) but it is hard to come up with a reasonable
formula for this as T may be anything from a large class and no subclass adds members to an empty class with beefy subclasses.
Instead The SZ value is set to some reasonable value similar to that of std::string. Applications can of course use type aliases to
create their own variants with some other default SZ.

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

