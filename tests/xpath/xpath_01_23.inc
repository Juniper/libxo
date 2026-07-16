+ data
= thing one
= thing two
= thing three
= thing four
= block 56
+ top-level
= type ethernet
= type bridge
= type 18u
= type 24
= address 0x0
= port 1
= address 0x0
= port 1
= address 0x0
= port 1
= used-percent 12
= kve_start 0xdeadbeef
= kve_end 0xcabb1e
= host my-box
= domain example.com
= host my-box
= domain example.com
= label value
= max-chaos very
= min-chaos 42
= some-chaos [42]
@ test-attr attr-value
[ sku
= sku gum-000-1412
= sku sum-000-4121
] sku
= host my-box
= domain example.com
@ test value
+ data
{ item
@ test2 value2
< item
@ test3 value3
$ sku GRO-000-415
$ name gum
= sold 1412
= in-stock 54
= on-order 10
> item
< item
@ test3 value3
$ sku HRD-000-212
$ name rope
= sold 85
= in-stock 4
= on-order 2
> item
< item
@ test3 value3
$ sku HRD-000-517
$ name ladder
= sold 0
= in-stock 2
= on-order 1
> item
< item
@ test3 value3
$ sku HRD-000-632
$ name bolt
= sold 4123
= in-stock 144
= on-order 42
> item
< item
@ test3 value3
$ sku GRO-000-2331
$ name water
= sold 17
= in-stock 14
= on-order 2
> item
} item
- data
+ data2
{ item
< item
$ sku GRO-000-415
$ name gum
= sold 1412.0
= in-stock 54
= on-order 10
> item
< item
$ sku HRD-000-212
$ name rope
= sold 85.0
= in-stock 4
= on-order 2
> item
< item
$ sku HRD-000-517
$ name ladder
= sold 0
= in-stock 2
= on-order 1
> item
< item
$ sku HRD-000-632
$ name bolt
= sold 4123.0
= in-stock 144
= on-order 42
> item
< item
$ sku GRO-000-2331
$ name water
= sold 17.0
= in-stock 14
= on-order 2
> item
} item
- data2
+ data3
{ item
< item
$ sku GRO-000-533
$ name fish
= sold 1321.0
= in-stock 45
= on-order 1
> item
} item
- data3
+ data4
{ item
@ test4 value4
= item gum
@ test4 value4
= item rope
@ test4 value4
= item ladder
@ test4 value4
= item bolt
@ test4 value4
= item water
} item
- data4
@ test value
+ data
{ item
@ test2 value2
< item
@ test3 value3
$ sku GRO-000-415
$ name gum
= sold 1412
= on-order 10
= in-stock 54
> item
< item
@ test3 value3
$ sku HRD-000-212
$ name rope
= sold 85
= extra special
= on-order 2
= in-stock 4
> item
< item
@ test3 value3
$ sku HRD-000-517
$ name ladder
= sold 0
= extra special
= on-order 1
= in-stock 2
> item
< item
@ test3 value3
$ sku HRD-000-632
$ name bolt
= sold 4123
= on-order 42
= in-stock 144
> item
< item
@ test3 value3
$ sku GRO-000-2331
$ name water
= sold 17
= extra special
= on-order 2
= in-stock 14
> item
} item
- data
= cost 425
= cost 455
= mode mode
= mode_octal octal
= links links
= user user
= group group
= pre that
= links 3
= post this
= mode /some/file
= mode_octal 640
= links 1
= user user
= group group
= one 1
= two 2
= three 3
= one 1
= two 2
= three 3
= one 1
= two 2
= three 3
- top-level
- data
