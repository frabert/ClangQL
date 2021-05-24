# ClangQL: query C++ codebases using SQLite and clangd

## What is it?

ClangQL is a proof-of-concept [SQLite](https://sqlite.org) extension for querying C++ codebases that have been indexed using [clangd](https://clangd.llvm.org/).

## How does it work?

It employs SQLite's virtual table system to act as an intermediary between SQLite and clangd's gRPC interface

## How do I use it?

Once the module has been built, you can load it in the sqlite3 CLI via the usual `.load clangql`.

Afterwards, you can connect to a codebase by instantiating the various virtual tables:

    sqlite> CREATE VIRTUAL TABLE llvm_symbols USING clangql (symbols, clangd-index.llvm.org:5900);
    sqlite> CREATE VIRTUAL TABLE llvm_base_of USING clangql (base_of, clangd-index.llvm.org:5900);
    sqlite> CREATE VIRTUAL TABLE llvm_overridden_by USING clangql (overridden_by, clangd-index.llvm.org:5900);
    sqlite> CREATE VIRTUAL TABLE llvm_refs USING clangql (refs, clangd-index.llvm.org:5900);

You can then query the codebase as if it was a regular table (some caveats apply, read the last point to learn more):

    sqlite> SELECT Name, Scope, DefPath FROM llvm_symbols WHERE Name = "Foo"
    Name  Scope                                        DefPath
    ----  -------------------------------------------  --------------------------------------------------------
    Foo   clang::clangd::                              clang-tools-extra/clangd/unittests/LSPBinderTests.cpp
    Foo   STLExtras_MoveRange_Test::TestBody()::Foo::  llvm/unittests/ADT/STLExtrasTest.cpp
    Foo   STLExtras_MoveRange_Test::TestBody()::Foo::  llvm/unittests/ADT/STLExtrasTest.cpp
    Foo   SizelessTypeTester::                         clang/unittests/AST/SizelessTypesTest.cpp
    Foo                                                llvm/unittests/ADT/TypeTraitsTest.cpp
    Foo                                                llvm/unittests/ADT/STLExtrasTest.cpp
    Foo   Class::                                      lldb/unittests/Utility/ReproducerInstrumentationTest.cpp
    Foo   llvm::orc::CoreAPIsBasedStandardTest::       llvm/unittests/ExecutionEngine/Orc/OrcTestCommon.h
    Foo   llvm::TrailingObjects::                      llvm/include/llvm/Support/TrailingObjects.h
    Foo                                                llvm/unittests/Support/BinaryStreamTest.cpp
    Foo   STLExtras_MoveRange_Test::TestBody()::Foo::  llvm/unittests/ADT/STLExtrasTest.cpp
    Foo                                                llvm/unittests/Support/BinaryStreamTest.cpp
    Foo   clang::                                      clang/unittests/AST/ASTTypeTraitsTest.cpp
    Foo                                                lldb/unittests/Utility/ReproducerInstrumentationTest.cpp

As another example, searching all the subclasses of a particular class:

    sqlite> SELECT subclass.Name, subclass.Scope, subclass.DefPath FROM llvm_symbols AS superclass INNER JOIN llvm_base_of AS rel ON rel.Subject = superclass.Id INNER JOIN llvm_symbols AS subclass ON subclass.Id = rel.Object WHERE superclass.Name = "MCAsmInfo";
    Name               Scope   DefPath
    -----------------  ------  ---------------------------------------------------
    NVPTXMCAsmInfo     llvm::  llvm/lib/Target/NVPTX/MCTargetDesc/NVPTXMCAsmInfo.h
    MCAsmInfoWasm      llvm::  llvm/include/llvm/MC/MCAsmInfoWasm.h
    BPFMCAsmInfo       llvm::  llvm/lib/Target/BPF/MCTargetDesc/BPFMCAsmInfo.h
    MockedUpMCAsmInfo          llvm/unittests/MC/SystemZ/SystemZAsmLexerTest.cpp
    AVRMCAsmInfo       llvm::  llvm/lib/Target/AVR/MCTargetDesc/AVRMCAsmInfo.h
    MCAsmInfoXCOFF     llvm::  llvm/include/llvm/MC/MCAsmInfoXCOFF.h
    MCAsmInfoDarwin    llvm::  llvm/include/llvm/MC/MCAsmInfoDarwin.h
    HackMCAsmInfo              llvm/unittests/CodeGen/TestAsmPrinter.cpp
    MCAsmInfoELF       llvm::  llvm/include/llvm/MC/MCAsmInfoELF.h
    MCAsmInfoCOFF      llvm::  llvm/include/llvm/MC/MCAsmInfoCOFF.h

Searching for all declarations inside of the `std` namespace:

    sqlite> SELECT decl.Name FROM llvm_symbols AS decl INNER JOIN llvm_refs AS ref ON ref.SymbolId = decl.Id WHERE decl.Scope = "std::" AND ref.Declaration = 1;
    Name
    ---------------------
    align_val_t
    __unexpected
    is_execution_policy
    __terminate
    is_execution_policy_v

In general, for each codebase four different virtual tables can be queried: a `symbols` table will contain information about every symbol in the codebase, a `base_of` table will contain information about what symbols are base classes of what symbols, a `overridden_by` table will contain information about what symbols are overridden by what symbols, and a `refs` table will contain information about symbol references.

The syntax for instantiating the tables is the following:

    CREATE VIRTUAL TABLE my_symbols USING clangql (symbols, host:port);
    CREATE VIRTUAL TABLE my_base_of USING clangql (base_of, host:port);
    CREATE VIRTUAL TABLE my_overridden_by USING clangql (overridden_by, host:port);
    CREATE VIRTUAL TABLE my_refs USING clangql (refs, host:port);

`my_*` names are not important and can be anything, the first parameter to the creation of the virtual tables is important and must be left as-is, the second parameter is the connection string. Currently, only unencrypted gRPC connections are supported.

## What's the schema?

The schema of `symbols` tables is equivalent to the following:

    CREATE TABLE vtable(Id TEXT, Name TEXT, Scope TEXT,
      Signature TEXT, Documentation TEXT, ReturnType TEXT,
      Type TEXT, DefPath TEXT, DefStartLine INT, DefStartCol INT,
      DefEndLine INT, DefEndCol INT, DeclPath TEXT,
      DeclStartLine INT, DeclStartCol INT, DeclEndLine INT, DeclEndCol INT,
      Kind INT, SubKind INT, Language INT,
      Generic INT, TemplatePartialSpecialization INT, TemplateSpecialization INT,
      UnitTest INT, IBAnnotated INT, IBOutletCollection INT, GKInspectable INT,
      Local INT, ProtocolInterface INT)

A textual representation for the `Kind`, `SubKind` and `Language` columns can be obtained using the `symbol_kind`, `symbol_subkind` and `symbol_language` functions.

Currently, the columns from `Generic` to `ProtocolInterface` are always 0, because for some reason the server always sends a zero-valued `properties` field.

The schema for `base_of` is the same as `overridden_by`, and is equivalent to the following:

    CREATE TABLE vtable(Subject TEXT, Object TEXT)

The meaning is as follows: if a row `(S, O)` is present in `base_of`, then `S` is a base class of `O`; if a row `(S, O)` is present in `overridden_by`, then `S` has been overridden by `O`.

Please note that it is only possible to query these two tables by their `Subject`, querying by `Object` is not possible due to limitations in the clangd protocol.

The schema of `refs` tables is equivalent to

    CREATE TABLE vtable(SymbolId TEXT, Declaration INT,
      Definition INT, Reference INT, Spelled INT,
      Path TEXT, StartLine INT, StartCol INT,
      EndLine INT, EndCol INT)

Please note that querying `refs` without a `SymbolId` will return 0 rows.

## How do I build it?

ClangQL uses CMake, Protocol Buffers and gRPC. On Windows I used vcpkg to manage the two dependencies. I'm afraid I'm not knowledgeable enough with Linux and/or macOS to give precise indications on how to build it there, but I'm guessing that as long as you have the correct development packages installed and visible on your system, CMake will be able to locate them.

Once the repository is cloned, run:

    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake

to configure the build. Adjust `CMAKE_BUILD_TYPE`, `VCPKG_TARGET_TRIPLET`, `CMAKE_TOOLCHAIN_FILE` and the generator type to suit your system and needs the best. Please note that the `sqlite` CLI tool and the extension must have the same bitness, at least on Windows. A 32-bit CLI (such as the precompiled one from SQLite.org) _will not_ load a 64-bit extension.

Once configured, run:

    cmake --build build

to compile the extension. First time will take a long time (on Windows), due to the need to compile Protobuf and gRPC as well. Later builds will be faster.

I have uploaded precompiled 32- and 64-bit DLLs for Windows as a GitHub release, anyways.

## What constraints are available?

On `symbols` tables, the following constraints will generate more specific requests to the clangd server:

- Equality on `Id`, `Name` or `Scope`
- `LIKE` on `Name`, `Scope`, `DefPath` or `DeclPath`

The `LIKE` constraint on `Name` relies on the fuzzy search semantics of clangd. The `LIKE` constraint on `Scope` has the effect of enabling the `any_scope` field of the fuzzy find request to clangd. The `LIKE` constraint on `DefPath` or `DeclPath` has the only effect of populating the `proximity_path` of the fuzzy find request to clangd, which has the ultimate effect of prioritizing symbols declared or defined near the specified path.

On `base_of` and `overridden_by` tables, only equality on `Subject` generates specific queries to the server.

On `refs` tables, only equality on `SubjectId` generates specific queries to the server.

## What works, what doesn't?

There is currently no way to i.e. obtain all possible relations between two symbols, so the relation tables are really only useful in joins. It's not a huge deal, as they are meant to be used that way anyways, but you still need to be careful when writing queries.

Not all queries are equally fast: querying on symbol id, name or scope is fast, everything else needs to happen client side and is potentially slow.

Similarly, when querying the `base_of` or `overridden_by` relations, only one of the two directions is possible, the other is not currently possible due to protocol limitations. Also, not specifying a `Subject` will result in 0 rows being returned.

Querying `refs` without specifying a `SymbolId` will result in 0 rows being produced. Specifying any one of `Definition`, `Declaration`, `Reference` or `Spelled` will generate more specific requests to the clangd server, all other fields are scanned client side.

Error checking is nonexistant. This is not ready for production use and was mostly made for fun, to explore to what extent the clangd interface was suitable for use with SQLite, and to learn about the SQLite virtual table system.
