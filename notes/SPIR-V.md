# SPIR-V

## Links

- Main Page: <https://www.khronos.org/spirv/>
- quick guide: <https://oneapi-src.github.io/level-zero-spec/level-zero/1.11/core/SPIRV.html>
- SPIRV-Tools Repo: <https://github.com/KhronosGroup/SPIRV-Tools>
- SPIRV-Guide Repo: <https://github.com/KhronosGroup/SPIRV-Guide>
- Subgroup tutorial: <https://www.khronos.org/blog/vulkan-subgroup-tutorial>

## Intro

### Types (`OpType`)

- `OpTypeVoid`
- _Scalar Types_
  - `OpTypeBool`
  - `OpTypeInt` with _Width_ equal to 8,16,32,64, both signed and unsigned _Signedness_
  - `OpTypeFloat` with _Width_ equal to 16 (half precision or brain floats),32 (single precision),64 (double precision)
- `OpTypeVector` with _Component Type_ that may be any _Scalar Type_, and _Component Count_ 2,3,4,8,16
- `OpTypeArray`
- `OpTypeStruct`: cannot nest `OpTypeStruct` (can use `OpTypePointer`)
- `OpTypeFunction`
- `OpTypePointer` pointer types (device addresses) (See buffer device address and untyped pointers)
  - can point only to data in specific *Storage Classes* (think of them as address spaces with a specific physical backing, such as in CUDA GMEM vs SMEM)
    - _CrossWorkgroup_, _Workgroup_, _UniformConstant_ (see later *Storage Classes*)

*Image DataType*: An image type can be _Depth_ and can be _Arrayed_

- only 2D and 3D can be depth image
- `OpTypeSampler` represents the sampler to fetch an image value
  - interpolation, normalized access, ...

| Dim    | Depth | Arrayed | Description             |
| ------ | ----- | ------- | ----------------------- |
| 1D     | 0     | 0       | A 1D image.             |
| 1D     | 0     | 1       | A 1D image array.       |
| 2D     | 0     | 0       | A 2D image.             |
| 2D     | 1     | 0       | A 2D depth image.       |
| 2D     | 0     | 1       | A 2D image array.       |
| 2D     | 1     | 1       | A 2D depth image array. |
| 3D     | 0     | 0       | A 3D image.             |
| Buffer | 0     | 0       | A 1D buffer image.      |

## Entry Points

A *Kernel* in a *SPIR-V Module* is an `OpFunction` identified in the *Module Header*
as a `OpEntryPoint`

An Entry Point has an _Execution Model_ (informaly shade type) and a _Name_

```shell
;; 2 entry points which actually execute the same function. when we create
;; a shader module in vulkan, we care about the string
OpEntryPoint GLCompute %main "name_b"
OpEntryPoint GLCompute %main "name_a"
```

To each entry point function, we can set a _Execution Mode_. For a compute shader,
that would be, for example, `LocalSize`, which is the _Size of Local Workgroup_,
which is the equivalent of a CUDA Block.

- 2 syntaxes, 1 with literals, 2 with variables (available from SPIR-V 1.2)
  1. example:

    ```shell
    ;; %main is the OpFunction used as OpEntryPoint
    OpExecutionMode %main LocalSize 1 1 1
    ```

  2. example:

    ```shell
    ;; example: %x, %y, %z are from a uniform buffer or push constant
    OpExecutionMode %main LocalSizeId %x %y %z
    ```

Another example of an _execution mode_ (which is not specific to compute shaders)
is the _Rounding Mode_, identified by `RoundingModeRTE` or `RoundingModeRTZ`, which
affect how the `OpFDiv` instruction operates

```shell
     ;; return variable space defined elsewhere (See Storage Classes and variables)
     OpEntryPoint Vertex %v_main "vertex_main" %vert_out
     OpEntryPoint Fragment %f_main "fragment_main"
     OpExecutionMode %v_main RoundingModeRTE 32
     OpExecutionMode %f_main RoundingModeRTZ 32
     ;; more stuff...

;; OpFunction -- OpFunctionEnd is the scope of the function
;; float foo(float bar) { return bar / 2.f; }
%foo = OpFunction %float None %1
%bar = OpFunctionParameter %ptr_float
  %2 = OpLabel
  %3 = OpLoad %float %bar        ;; load from memory the bar param
  %4 = OpFDiv %float %3 %float_2 ;; divide by 2
       OpReturnValue %4
       OpFunctionEnd
;; if %foo is called from %v_main, it uses RoundingModeRTE
;; if %foo is called from %f_main, it uses RoundingModeRTZ
```

### LocalSize and WorkGroupSize

`GLCompute` (Vulkan/WebGPU) and `Kernel` (SYCL/OpenCL) _Execution Models_
both accept _Execution Modes_ `LocalSize` or `LocalSizeId` and `WorkGroupSize`.

All of these set the _Local Workgroup_ Size, which is the *Vulkan Equivalent of a
CUDA Block*.

- Global Workgroup -> CUDA Grid
- WorkgroupSize is deprecatd.

*Note*: using LocalSize means that the block size is baked at shader compilation,
which is not what we want to achieve a CUDA-like behaviour, therefore, we turn
towards `LocalSizeId`, which instead defines the block size through 
*specialization constants*, which is set at *Shader Module Creation Time*

- requires either `VK_KHR_maintenance4` device extension or Vulkan 1.3
- if you need to change it, you'll thrash the whole `VkPipeline`, unless 
 dynamic state is used
- specialization constants are not the only way? See later

Examples on how to set it from github docs

```c
// Block/Local Workgroup size baked into the shader
// GLSL
#version 450
layout (local_size_x = 2, local_size_y = 4, local_size_z = 1) in;
void main() { }

// SPIR-V
OpExecutionMode %main LocalSize 2 4 1
```

```c
// Block/Local Workgroup size through specialization constants
// GLSL (if one axis is not specified, 1 is default)
#version 450
layout (local_size_x_id = 3, local_size_y_id = 4) in;
void main { }

// SPIR-V
// only instructions inside function blocks are to be kept in order, declarations
// can be out of order (some sort of hoisting)
     OpExecutionModeId %main LocalSizeId %7 %8 %uint_1
     OpDecorate %7 SpecId 3 ;; specify where the value comes from, doesn't fetch
     OpDecorate %8 SpecId 4
%7 = OpSpecConstant %uint 1 ;; this fetches the values
%8 = OpSpecConstant %uint 1
%uint_1 = OpConstant %uint 1 ;; declare an alias for constant unsigned int 1
```

## Types Examples

SPIR-V's type system is _explicit_. Every type needs to be specified through
`OpType` declarations

```shell
;; all constraints have been specified above
;; define a floating point of 32 bits
%float = OpTypeFloat 32
;; define a vector of 2 floats (component type followed by component number)
%v2float = OpTypeVector %float 2
;; That's a type we didn't specify before!
;; takes the column type and number of columns, _independently of memory layout_
;; By default it's column-major, hence takes the column type and number of columns
;; - 3 consecutive vec2s
%mat3x2float = OpTypeMatrix %v2float 3
;; if you need row-major, the _variable_, not type, needs to be decorated with RowMajor
OpMemberDecorate %myStruct 0 RowMajor ;; take first member in struct, that is row major
```

Struct Example

```shell
;; struct myStruct { int a; float b; int c;  }
     %int = OpTypeInt 32 1 ;; bits and signedness
   %float = OpTypeFloat 32 ;; bits
%myStruct = OpTypeStruct %int %float %int

;; Note: We also need to specify the offsets in bytes!
;; again hoisting: OpMemberDecorate/OpDecorate can come before or after (usually before) definitions of types
;; You need to follow rules eg std140 or std430 when buffer types (more on that later)
OpMemberDecorate %myStruct 0 Offset 0
OpMemberDecorate %myStruct 1 Offset 4
OpMemberDecorate %myStruct 2 Offset 8
```

Note that struct indices and offsets are not strictly increasing and matching. Example from GitHub

```c
// GLSL
layout(binding = 0) uniform ubo_b {
  layout(offset = 4) float x;
  layout(offset = 8) float y;
  layout(offset = 0) flaot z;
} B;

// SPIR-V
%float = OpTypeFloat 32
%ubo_b = OpTypeStruct %float %float %float

OpMemberDecorate %ubo_b 0 Offset 4
OpMemberDecorate %ubo_b 1 Offset 8
OpMemberDecorate %ubo_b 2 Offset 0
```

## SPIR-V File Format

See this link

- https://github.com/KhronosGroup/SPIRV-Guide/blob/main/chapters/spirv_internals.md

And Specification. Reported here is the logical layout for a SPIR-V Module

1. `OpCapability`
2. `OpExtension` (optionally declare you're using some extensions)
3. `OpExtInstImport` (import instructions from extensions)
4. `OpMemoryModel` Specify the memory model you're using (always Vulkan)
5. All `OpEntryPoint`
6. All execution modes with `OpExecutionMode` or `OpExecutionModeId`
7. Debug Instructions (later)
8. Annotation Instructions (`OpDecorate`, `OpMemberDecorate`)
9. Type Declarations `OpType..`, Constants, Global Variables `OpVariable`
10. Function forward declarations with `OpFunction`, `OpFunctionParameter` and `OpFunctionEnd`
11. Function definitions. Same as declarations, but before end there's a sequence of *block*s.
  - Each block starts with `OpLabel` and terminates with a _Block Termination Instruction_ (multiple) and terminates with a _Block Termination Instruction_ (multiple) and terminates with a _Block Termination Instruction_ (multiple)
  - all `OpVariable` follow the label and must have *Storage Class* (coming) as `Function`

- Function call: `OpFunctionCall`
- since SPIR-V follows the *Static Single Assignment* phylosophy, how do we do something like
  ```
  if (cond)
    x = 1
  else
    x = 2
  use(x)
  ```
  We use the `OpPhi` Instruction
  ```
  %result = OpPhi %type %value_from_A %block_A %value_from_b %block_b
  ```
  Hence that block of pseudocode becomes
  ```
  ;; assume we defined our types before
  ;; assume %cond is a predicate defined somewhere, while %then and %else are labels defined later (hoisting)
  %entry = OpLabel
           OpSelectionMerge %merge None ;; selection header for structured control flow
           OpBranchConditional %cond %then %else ;; block terminator

   %then = OpLabel
     %v1 = OpConstant %int 1
           OpBranch %merge                       ;; block terminator

   %else = OpLabel
     %v2 = OpConstant %int 2
           OpBranch %merge   ;; Block Terminator (even if instruction follows!)

  %merge = OpLabel
      %x = OpPhi %int %v1 %then %v2 %else
  ;; some rules: 
  ;; - all blocks are immediate parents to %merge block, and ALL of them need to contribute. 
  ;; - All values must have same type
  ;; - OpPhi must precede all non-OpPhi instructions within the block
  ```

  Another OpPhi Example: Loop

  ```c
  int i = 0;
  while (i < 10) { i++; }
  ```

  ```
     %int = OpTypeInt 32 1
    %bool = OpTypeBool

    %zero = OpConstant %int 0
     %one = OpConstant %int 1
     %ten = OpConstant %int 10

   ;; Stuff... (there should be an OpFunction here)

   %entry = OpLabel ;; Loop header block

   ;; Stuff...

            ;; when next block is a Structured control flow construct (see later)
            ;; the instruction preceding the OpBranch/OpConditionalBranch must be
            ;; OpLoopMerge <MergeBlockId> <ContinueBlockId> <ControlParameters, eg None, Unroll, MaxIterations, ...>
            ;; we have a continue block because we cannot escape the loop without going through the "back edge" to loop header
            OpLoopMerge %exit %body None ;; last block instruction, prelude to Structured Control Flow
            OpBranch %loop ;; block terminator

    %loop = OpLabel ;; Back-Edge Block
       %i = OpPhi %zero %entry %i_next %body ;; take %zero if we came from %entry, otherwise take %i_next
    %cond = OpSLessThan %bool %i %ten        ;; less than signed. return as boolean
            OpBranchConditional %cond %body %exit ;; block terminator. If %cond go to %loop

    %body = OpLabel ;; Continuation Construct (in this case a single block)
  %i_next = OpIAdd %int %i %one ;; Result ID = Integer addition
            OpBranch %loop ;; block terminator

    %exit = OpLabel
  ;; Some Stuff...
  ```

  As you can see, SPIR-V is defined such that it explicitly defines a *Control Flow Graph*

  This is the concept of *Structured Control Flow* (2.11):

  - Header block (before *divergence* (Explicit management of warp divergence!))
  - Branch blocks
  - Merge block

## Structured Control Flow Constructs

There are 3 (5) fundamental structured control flow constructs, introduced by a
*Merge Instruction* as a second-to-last instruction inside a *header block".

Types:

1. *Selection Construct* a selection header (`OpSelectionMerge`+`OpBranchConditional`) 
   precedes a branch block and a merge block
2. *Loop Construct*: Loop Header + Continue Construct + Back Edge Block (exiting condition)
  - *Continue Construct* blocks whose all incoming path pass through the `OpLoopMerge` Continue Target (Second Operand).
    and all outgoing paths lead to the *back-edge* (the block which is after the loop header (block with OpLoopMerge) and performs the check)
    - Synonym: _Structurally dominated by Continuation Target_, _Structurally Post Dominated By Back-edge block_
4. *Switch Construct*
  - _Switch Header_: `OpSelectionMerge` + `OpSwitch`
  - *Case Construct*: Structurally dominated (it follows) an `OpSwitch` _Target_ or _Default_ block
    - `OpSwitch <SelectorId> <DefaultBLockId> <literal 1> <Block Id 1> ...`
    - <SelectorId> must be an integer and literal values are declared constants

### Examples: Selection Construct

```c
if (cond) {
  a = 1
} else {
  a = 2
}
```

```shell
;; Somewhere in the types and constants declaration
       %int = OpTypeInt 32 1 ;; bits and signedness
      %bool = OpTypeBool
 
       %one = OpConstant %int 1
       %two = OpConstant %int 2
 
;; ...

;; inside an OpFunction, let %cond be the OpTypeBool driving divergence
     %entry = OpLabel
;; something... 
              OpSelectionMerge %merge None
              OpConditionalBranch %cond %cond_true %cond_false
 
 %cond_true = OpLabel
    %a_true = OpCopyObject %int %one ;; New Instruction!
              OpBranch %merge

%cond_false = OpLabel
   %a_false = OpCopyObject %int %two
              OpBranch %merge

     %merge = OpLabel
         %a = OpPhi %int %a_true %cond_true %a_false %cond_false
;; continue ...
```

### Examples: Loop

```c
while (i < 10) { i++; }
```

```shell
;; somewhere in the types and constants declaration
     %int = OpTypeInt 32 1
    %bool = OpTypeBool
 
    %zero = OpConstant %int 0
     %ten = OpConstant %int 10
 
;; inside an OpFuction definition
   %entry = OpLabel
;; something
            OpLoopMerge %exit %continue None
            OpBranch %header
 
  %header = OpLabel
       %i = OpPhi %int %zero %entry %i_next %continue
    %cond = OpSLessThan %bool %i %ten
            OpBranchConditional %cond %body %exit
 
    %body = OpLabel
  %i_next = OpIAdd %int %i %one
            OpBranch %continue ;; just to be explicit, could be removed
 
%continue = OpLabel
            OpBranch %header

    %exit = OpLabel
;; something ... 
```

### Examples: switch

```c
switch (x) {
 case 0:   a = 1; break;
 case 1:   a = 2; break;
 default:  a = 3; break;
}
```

```shell
;; somewhere in the types,constants,global variables definitions
    %int = OpTypeInt 32 1
   %bool = OpTypeBool

;; then, inside a OpFunction

  %entry = OpLabel

;; something... (let %x be an %int)
  
           OpSelectionMerge %merge None
           OpSwitch %x %x_default 0 %case0 1 %case1
  
  %case0 = OpLabel
     %a0 = OpConstant %int 1
           OpBranch %merge
  
  %case1 = OpLabel
     %a1 = OpConstant %int 2
           OpBranch %merge
  
%default = OpLabel
     %ad = OpConstant %int 3
           OpBranch %merge

  %merge = OpLabel
      %a = OpPhi %int %a0 %case0 %a1 %case1 %ad %default
;; continue
```

## Storage Classes

The *Storage Class* is an _operand_ used in 

- *Variable Declarations* `OpVariable`
- *Pointer Type Declaration* `OpTypePointer`
- The rest, eg untyped pointers, on documentation

And dictates 

- _Where Data Lives in Memory_
- How long does the data persist
- Which *Shader Invocations* can see it

Fundamental Concepts

- *Invocation*: An execution of an entrypoint of a SPIR-V Execution Model by a thread 
  - in compute models (GLCompute/Kernel): operates on a single data item (or grid stride loop)
  - in a vertex execution model: processes a single vertex
- *Subgroup*: Invocations are partitioned into _subgroups_, which map to the warp/wave wide "physical" execution
  of GPU devices. Its size is defined by the `SubgroupSize` and `SubgroupMaxSize` *build-in variables*
  - in compute models (GLCompute/Kernel) Subgroups are grouped into _Workgroups_
  - can synchronize and share data with each other (*Warp Intrinsics* (later))
- *Workgroup*: partitioning of compute execution model done into compute execution models.
  It is the SPIR-V Equivalent of a CUDA Block.
  - 3D Size retrieved by `WorkgroupSize` built-in (_Deprecated_) or `LocalSize`,`LocalSizeId` Execution Modes
  - can synchronize and exchange data among them (shared memory)
- *Tangle*: Equivalent of CUDA's activemask, ie the set of invocations that
  execute the same dynamic instance of an instruction

### Storage Classes: Definitions and Examples

We refer as `Value` to the binary value found in the compiled SPIR-V File

#### Storage Classes: Definitions and Examples: Input and Output

Storage Classes handling input and output of an Execution Model

- `Input`
  - Value: 1
  - GLSL Equivalent: `in`
  - Semantics: *Read-Only* data provided by previous pipeline stage or input assembler
    - Visible only to *Current Invocation*
    - _Vertex Shader_: Vertex Attributes
    - _Fragment Shader_: Interpolated values from the _Rasterizer_
- `Output`
  - Value: 3
  - GLSL Equivalent: `out`
  - Semantics: *Write-Only* data passed to next stage.
    - Visible only to *Current Invocation*

```shell
;; GLSL: layout(location = 0) in vec3 position;
             %float = OpTypeFloat 32
           %v3float = OpTypeVector %float 3
;; The Storage Class!
%_ptr_Input_v3float = OpTypePointer Input %v3float

;; Variable Declaration
          %position = OpVariable %_ptr_Input_v3float
```

#### Storage Classes: Definitions and Examples: Global & Constant Memory

Resources shared across invocations, usually backed by buffers/images and
described/accessed by/through descriptors

- `UniformConstant`
  - Value: 0
  - GLSL Equivalent: `uniform sampler2D` (for a 2D Sampled Texture)
  - Semantics: *Read-Only* data, used for _opaque handlers_ like _Samplers_, _Texture_, _Atomic Counters_
    - Visible to *All Invocations*
- `Uniform`
  - Value: 2
  - GLSL Equivalent; `uniform Block { ... } block`
  - Semantics: *Read-Only* Buffer Memory, with an _Explicit Layout_ defined through decorators. (Offset, Strides,)
    - Used for UBOs (Uniform Buffer Objects)
    - Visible to *All Invocations*
- `StorageBuffer`
  - Value: 12
  - GLSL Equivalent: `buffer Block { ... } block`
  - Semantics: *Read/Write* Buffer memory. Used for SSBOs (Shader Storage Buffer Objects)
    - Visible to *All Invocations*
    - (CUDA:) Can be much larger than uniform storage classes, as they map to Constant Memory and Texture Memory,
      while `StorageBuffer` Maps to *Global Memory*
- `PushConstant`
  - Value: 9
  - GLSL Equivalent: `layout(push_constant) uniform ...`
  - Semantics: Small, *Read-Only* bank of values pushed directly via the API (Command Buffer `vkCmdPushConstants`)

```shell
;; GLSL: layout(push_constant) uniform Constants { mat4 viewProj; } pc;
;; requires the Matrix Capability (which is implied by the Shader Capability) (more later)
                  %float = OpTypeFloat 32
                %v4float = OpTypeVector %float 4
                   %mat4 = OpTypeMatrix %v4float 4
                  %Block = OpTypeStruct %mat4
%_ptr_PushConstant_Block = OpTypePointer PushConstant %block

;; Define the block layout (decorations omitted but necessary, ie stride, offset if more fields, ...)
                     %pc = OpVariable %_ptr_PushConstant_Block
```

#### Storage Classes: Definitions and Examples: Local & Private Memory

Variables created during the execution of a shader

- `Function`
  - Value: 7
  - GLSL Equivalent: Local variables like `vec3 tmp = ...;`
  - Semantics: Temporary variables inside an `OpFunction`. Lifetime ends when
    function returns
    - Visible _only_ to the _Current Invocation_
- `Private`
  - Value: 6
  - GLSL Equivalent: Global variables like `vec3 globalVec;`
  - Semantics: Used for global variable helpers which don't reset between function calls.
    - Visible _only_ to the _Current Invocation_ thread
- `Workgroup`
  - Value: 4
  - GLSL Equivalent: `shared` keyword
  - Semantics: *Shared Memory* visible to all invocations _Within the same workgroup_
    - Requires Compute Capability (GLCompute and Kernel Execution Models have that)
    - Maps to *LDS* (Local Data Storage) (AMD) and *SMEM* (Shared Memory) (NVIDIA)

```shell
;; GLSL, inside a function: shared float sharedData[256];
;; inside type and variable declarations
              %uint = OpTypeInt 32 0
          %uint_256 = OpConstant %uint 256
             %float = OpTypeFloat 32
    %_arr_float_256 = OpTypeArray %float %uint_256
%_ptr_Workgroup_arr = OpTypePointer Workgroup %_arr_float_256
;; omitted all type decorations
;; ...
;; now inside an OpFunction
;; Note: storage class declared on the OpVariable must match the one in the OpTypePointer if the OpVariable is a OpTypePointer
        %sharedData = OpVariable %_ptr_WorkGroup_arr Workgroup
```

#### Storage Classes: Definitions and Examples: Recap

| Storage Class   | Scope/Lifetime               | Visibility           | Read/Write? |
| --------------- | ---------------------------- | -------------------- | ----------- |
| `Function`      | Function Execution           | Current Thread       | RW          |
| `Private`       | Shader Execution             | Current Thread       | RW          |
| `Input`         | Shader Invocation            | Current Thread       | RO          |
| `Output`        | Shader Invocation            | Current Thread       | WO          |
| `Workgroup`     | Workgroup Execution          | Threads in Workgroup | RW          |
| `Uniform`       | Draw/Dispatch                | All Threads          | RO          |
| `StorageBuffer` | Application Defined Lifetime | All Threads          | RW          |
| `PushConstant`  | Command Buffer Defined       | All Threads          | RO          |

### Storage Classes: Important Constraints

1. *Decorations* Who requires decorations
  - `Uniform`, `StorageBuffer`, `PushConstant` usually point to structs. Structs
    require `Block` or `BufferBlock` decorations and explicit member offsets (`OpMemberDecorate ... Offset ...`)
2. *Initializers* Who can be initialized with a value
  - `Input`, `Output`, `PushConstant`, `Uniform` variables *Cannot* have initializers
  - `Function`, `Private` variables *can* have initializers
3. *Logical vs Physical* (links to memory model, `OpMemoryModel`)
  - *Logical* _Addressing Model_: Cannot perform pointer arithmetic
  - *Physical* _Addressing Model_: Can perform pointer arithmetic

See Memory Model for more information on the last bullet.

### Storage Classes: Comparison with CUDA

A Storage Class defines the logical address space in which some data lives.

- Defines the lifetime and visibility of such variable

| SPIR-V Storage Class | CUDA Memory Space        | Hardware Location                          | Characteristics                                         |
| -------------------- | ------------------------ | ------------------------------------------ | ------------------------------------------------------- |
| Private              | Registers / Local Memory | On-chip Registers (or spill to L1/L2/GMEM) | Fastest. Unique to one thread.                          |
| Function             | Local Memory (Stack)     | Register File / L1 Cache                   | Thread-local, scoped to function lifetime.              |
| Workgroup            | Shared Memory (SRAM)     | On-chip L1/Shared Memory (CTA-local)       | High bandwidth, low latency. Visible to a thread block. |
| Uniform              | Constant Memory          | Dedicated Constant Cache / L1              | Read-only, optimized for broadcast.                     |
| StorageBuffer        | Global Memory            | VRAM / Device Memory                       | Read/Write, large capacity, visible to all threads.     |
| UniformConstant      | Texture / Surface        | Texture Cache / L1                         | Optimized for spatial locality and hardware filtering.  |
| CrossWorkgroup       | Global Memory            | VRAM (OpenCL specific)                     | Shared across all workgroups in a kernel.               |

#### Storage Classes: Comparison with CUDA: Shared Memory Example

`__shared__` in SPIR-V is mapped to the `Workgroup` storage class

```c
__shared__ uint32_t shared_mem-var[64];
```

```shell
          %uint = OpTypeInt 32 0
       %uint_64 = OpConstant %uint 64
      %arr_type = OpTypeArray %uint %uint_64
 %ptr_Workgroup = OpTypePointer Workgroup %arr_type
;; global SMEM variable
%shared_mem_var = OpVariable %ptr_Workgroup Workgroup
```

#### Storage Classes: Comparison with CUDA: Global Memory Example

Standard Device Memory pointers statically declared with `__device__` or `cudaMalloc*`
map to `CrossWorkgroup` (OpenCL) or `StorageBuffer` (Vulkan/OpenGL/Direct3D12)

```c
__global__ void someKernel(..., uint32_t* global_buffer, ...) {...}
```

```shell
;; StorageBuffer Struct Requires decorations
             %uint = OpTypeInt 32 0
       %StructType = OpStruct %uint
                     OpDecorate %StructType Block
                     OpMemberDecorate %StructType 0 Offset 0
%ptr_StorageBuffer = OpTypePointer StorageBuffer %StructType
    %global_buffer = OpVariable %ptr_StorageBuffer StorageBuffer

;; assume %main is an OpFunction defined somewhere
                     OpEntryPoint GLCompute %main "someKernel"
```

### Storage Classes: Final Example: Copying from StorageBuffer → Workgroup

High level GLSL

```
layout(set = 0, binding = 0) buffer Buf {
  float data[256];
} buf; 

shared float sharedData[256];

void main() {
  uint i = gl_LocalInvocationID.x;
  sharedData[i] = buf.data[i];
}
```

SPIR-V snippet

```
;; --------- Capabilities and Memory Model ----------
OpCapability Shader
OpMemoryModel Logical GLSL450 ;; we'll switch towards a Physical, Vulkan later

;; --------- Types ----------------------------------
%void  = OpTypeVoid
%uint  = OpTypeInt 32 0
%float = OpTypeFloat 32

%uint_0   = OpConstant %uint 0
%uint_256 = OpConstant %uint 256

%v3uint = OpTypeVector %uint 3
%fn     = OpTypeFunction %void ;; function which returns void and takes no params

%arr_float_256 = OpTypeArray %float %uint_256 ;; float array of 256

%ptr_Input_v3uint        = OpTypePointer Input %v3uint ;; Input: gl_LocalInvocationID
%ptr_StorageBuffer_float = OpTypePointer StorageBuffer %float ;; array of 256 floats in global memory
%ptr_Workgroup_float     = OpTypePointer Workgroup %float ;; SMEM float variable
%ptr_Workgroup_arr       = OpTypePointer Workgroup %float ;; SMEM array of 256 float 

;; Storage buffer array of structs of float array 
%StructType = OpStruct %arr_float_256
              OpDecorate %StructType Block
              OpMemberDecorate %StructType 0 Offset 0
%_ptr_StorageBuffer_arr = OpTypePointer StorageBuffer %StructType

;; --------- Interface Variables --------------------
%gl_LocalInvocationID = OpVariable %ptr_Input_v3uint Input

;; StorageBuffer Block
%buf = OpVariable %_ptr_StorageBuffer_arr StorageBuffer
;; Descriptor Decorations omitted!

;; Workgroup shared memory
%sharedData = OpVariable %ptr_Workgroup_arr Workgroup

;; --------- Functions ------------------------------
   %main = OpFunction %void None %fn ;; -----

           ;; uint i = gl_LocalInvocationID.x;
  %entry = OpLabel
    %lid = OpLoad %v3uint %gl_LocalInvocationID
  %lid_x = OpCompositeExtract %uint %lid 0 ;; straightforward instruction
           ;; float val = buf.data[i]
%buf_ptr = OpAccessChain %ptr_StorageBuffer_float %buf %uint_0 %lid_x ;; take source and a list of indices. Apply indexing operations in sequence. In this case 1) Access the 0th member of the struct 2) Move pointer to lid_x element 3) Still a pointer, cause we didn't dereference it with an OpLoad
    %val = OpLoad %float %buf_ptr ;; OpAccessChain to index + OpLoad to dereference is a key formula for data access
           ;; sharedData[i] = val
 %sh_ptr = OpAccessChain %ptr_Workgroup_float %sharedData %lid_x
           OpStore %sh_ptr %val ;; when there's no result id, there's no type specification

           OpFunctionEnd             ;; -----

```

## Capabilities

## Vulkan Memory Model

TODO

```shell
OpCapability VulkanMemoryModel
OpMemoryModel Logical Vulkan
;; versus default OpMemoryModel Logical GLSL450
```

## Debugging

- Ref: <https://github.com/KhronosGroup/SPIRV-Guide/blob/main/chapters/shader_debug_info.md>



