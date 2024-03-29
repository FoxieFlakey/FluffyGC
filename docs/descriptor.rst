``fh_descriptor*``
##################

Descriptor about given object

The definition is

.. code-block:: c

   enum fh_reference_strength {
     FH_REF_STRONG
   };
   
   typedef struct {
     const char* name;
     size_t offset;
     const char* dataType;
     enum fh_reference_strength strength;
   } fh_descriptor_field;
   
   typedef struct {
     size_t size;
     
     // Called somewhere in the future after
     // losing last reference.
     // objData is given as direct access
     // read only but references still cant
     // be read even if its allowed it adds
     // complexities to ensure its valid
     @Nullable
     fh_finalizer finalizer;
     
     fh_descriptor_field fields*;
   } fh_descriptor_param;

For convenience following macro must present
.. code-block:: c

   #define FH_FIELD(type, member, _dataType, refStrength) \
    { \
      .name = #member, \
      .offset = offsetof(type, member), \
      .dataType = (_dataType), \
      .strength = (refStrength) \
    }
   #define FH_FIELD_END() {.name = NULL}

Descriptor Names
****************
Descriptor names (e.g. ``net.hostname.app.Test``) follows Java constructs
Names under ``fox.fluffyheap.*`` are for API reservation

Ungettable names are ``fox.fluffyheap.marker.*`` these merely serves as marker
for certain usages and cannot be get via `fh_get_descriptor`_ 

Packages seperated by dot
Regex of valid descriptor name parts ``[a-zA-Z_$][a-zA-Z0-9_$.]*``
Examples
  ``test.test.Class`` is valid
  ``09test`` is not
  ``hello.hi.UwU`` is valid
  ``Hello`` is valid

Markers
*******
``fox.fluffyheap.marker.Any`` - Essentially like ``void*`` program can assign any 
                                object in field with this type

Notes about fields
******************
Field must atomic like this (this the only constraint placed 
on the program's structure to be used with fluffy heap)

.. code-block:: c

   struct list_node {
     _Atomic(struct list_node*) next; // Like this and must be pointer to 
     _Atomic(struct list_node*) prev; // the type the program wanted
   };

This placed to have foundation for whatever implementation
which need atomic access to fields for their purpose same for
arrays if implementation implements DMA (each element location
in reference array is ``_Atomic`` specified which let the
implementations to use atomic operations)

Functions
#########

+--------------------------+-----------------------------------------------------------------------------------------------+--------------------------+--------------------------+
| Return value             | Function name                                                                                 | Link                     | Link                     |
+==========================+===============================================================================================+==========================+==========================+
| int                      | fh_define_descriptor(fh_descriptor** outDesc, const char* name, fh_descriptor_param* parameter, bool dontInvokeLoader)   | `fh_define_descriptor`_  |
+--------------------------+-----------------------------------------------------------------------------------------------+--------------------------+--------------------------+
| @Nullable fh_descriptor* | fh_get_descriptor(const char* name, bool dontInvokeLoader)                                                               | `fh_get_descriptor`_     |
+--------------------------+-----------------------------------------------------------------------------------------------+--------------------------+--------------------------+
| void                     | fh_release_descriptor(@Nullable fh_descriptor* desc)                                                                     | `fh_release_descriptor`_ |
+--------------------------+-----------------------------------------------------------------------------------------------+--------------------------+--------------------------+

``fh_define_descriptor`` and ``fh_get_descriptor`` only valid for object
descriptor not array as array differ.

Creation of array using descriptors is illegal please use
appropriate array constructors.

Methods
#######

+--------------------------------+----------------------------------------------+----------------------------+
| Return value                   | Method name                                  | Link                       |
+================================+==============================================+============================+
| @ReadOnly fh_descriptor_param* | fh_descriptor_get_param(fh_descriptor* self) | `fh_descriptor_get_param`_ |
+--------------------------------+----------------------------------------------+----------------------------+

Function details
################

fh_define_descriptor
********************
.. code-block:: c

   int fh_define_descriptor(const char* name, fh_descriptor_param* parameter, bool dontInvokeLoader)

Define a descriptor named "name" and acquire it (to prevent being GC-ed). Must be
able handle circular references.

Since
=====
Version 0.1

Parameters
==========
  ``name`` - Name for the descriptor (follows Java convention like ``lua.lang.Table`` for example)
  ``parameter`` - Other parameters describing the layout and requirements
  ``dontInvokeLoader`` - Whether to invoke loader or not

Return
======
0 on success 
Error:
  -EINVAL: Invalid name or parameter
  -ENOMEM: Not enough memory
  -EEXIST: Already defined

Tags
=====
GC-Safepoint GC-May-Invoke Need-Valid-Context

fh_get_descriptor
********************
.. code-block:: c

   @Nullable
   fh_descriptor* fh_get_descriptor(const char* name, bool dontInvokeLoader)

Get a descriptor named "name" or call application
defined hook to load if not present and acquire it
(to prevent being GC-ed). Calling application hook
can recurse forever and its valid so application
must ensure there no recursing

There few requirements:
1. Must not call app loader for ``fox.fluffyheap.*`` regardless ``dontInvokeLoader``
   as these reserved by specification and may get added or removed, and may be treated
   differently than normal descriptors thus it don't make any sense for app loader to
   load them
2. GC still able to run inside app's descriptor loader

And constraints too:
1. Cannot get array descriptor (returns NULL)

Since
=====
Version 0.1

Parameters
==========
  ``name`` - Name for the descriptor (follows Java convention like ``lua.lang.Table`` for example)
  ``dontInvokeLoader`` - Whether to invoke or not invoke app's loader possibly for avoiding recursion

Return
======
The descriptor. NULL if name invalid or non-existentent descriptor

Tags
=====
GC-Safepoint GC-May-Invoke Need-Valid-Context May-Block

fh_release_descriptor
*********************
.. code-block:: c

   void fh_release_descriptor(@Nullable fh_descriptor* self)

Release the descriptor so it can be GC-ed. After this
call usage of ``self`` considering undefined beahaviour.
or do nothing if ``self`` is NULL. 

Since
=====
Version 0.1

Parameters
==========
  ``self`` - Descriptor to release

Tags
=====
GC-Safepoint Need-Valid-Context

Method details
##############

fh_descriptor_get_param
***********************
.. code-block:: c

   const fh_descriptor_param* fh_descriptor_get_param(fh_descriptor* self)

Gets read only parameter for the ``self`` descriptor. The ``fields``
field will be non NULL. Which lives as long ``fh_descriptor*`` still
acquired (by ``descriptor_acquire``)

Since
=====
Version 0.1

Parameters
==========
  ``self`` - Descriptor to retrieve parameter from

Return value
============
The requested parameters read only

Tags
=====
GC-Safepoint Need-Valid-Context
