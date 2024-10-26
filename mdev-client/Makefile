# apt install llvm-dev clang lld libc++-dev libc++abi-dev libssl-dev intel-oneapi-ippcp-devel libfmt-dev

USE_CLANG?=1
USE_LIBCXX?=0
USE_MIMALLOC?=1
USE_MIMALLOC_DYNAMIC?=1

ifeq ($(USE_CLANG), 1)
	CC=clang-12
	CXX=clang++-12
	LD=lld-12
	AR=llvm-ar-12
	LDFLAGS+=-fuse-ld=$(LD)

	ifeq ($(USE_LIBCXX), 1)
		CFLAGS+=-stdlib=libc++
		CXXFLAGS+=-stdlib=libc++
	endif
else
	USE_LIBCXX=0
	CC=gcc
	CXX=g++
	AR=gcc-ar
endif

CPPFLAGS+=-D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -MMD -MP
CPPFLAGS+=-I/lib/modules/$(shell uname -r)/build/usr/include -Iinclude
CFLAGS+=-Wall -Wextra -Wformat=2 -Wshadow -Werror=return-type -std=c11 -fwrapv
CXXFLAGS+=-Wall -Wextra -Wformat=2 -Wshadow -Werror=return-type -std=c++20 -fwrapv

IPPCRYPTOROOT?=/opt/intel/oneapi/ippcp/latest
IPPCP_TARGET_ARCH?=intel64
CPPFLAGS+=-I$(IPPCRYPTOROOT)/include
LDFLAGS+=-L$(IPPCRYPTOROOT)/lib/$(IPPCP_TARGET_ARCH)

MIMALLOCROOT?=$(realpath ../mimalloc)
CPPFLAGS+=-I$(MIMALLOCROOT)/include
LDFLAGS+=-L$(MIMALLOCROOT)/out/release
LDLIBS+=-lmimalloc

BOOST_ROOT?=../boost_1_83_0
CPPFLAGS+=-I$(BOOST_ROOT)
LDFLAGS+=-L$(BOOST_ROOT)/stage/lib

CPPFLAGS+=-pthread
LDLIBS+=-pthread

SGX_SDK?=/opt/intel/sgxsdk
CPPFLAGS+=-I$(SGX_SDK)/include -Iinclude/sgx

ifeq ($(DBGPRINT), 1)
	CPPFLAGS+=-DDBGPRINT=1
endif

ifeq ($(DEBUG), 1)
	CPPFLAGS+=-DDEBUG=1
	CFLAGS+=-O0 -g3 -fno-omit-frame-pointer
	CXXFLAGS+=-O0 -g3 -fno-omit-frame-pointer
else ifeq ($(PROFILE), 1)
	CPPFLAGS+=-DNDEBUG -DPROFILE=1
	CFLAGS+=-O2 -g3 -pg
	CXXFLAGS+=-O2 -g3 -pg
else ifeq ($(SANITIZE), 1)
	CPPFLAGS+=-DNDEBUG
	CFLAGS+=-Og -g3 -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer
	CXXFLAGS+=-Og -g3 -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer
	ifneq ($(USE_CLANG), 1)
		CFLAGS+=-fanalyzer
		CXXFLAGS+=-fanalyzer
	endif
else ifeq ($(FASTBUILD), 1)
	CPPFLAGS+=-DNDEBUG
	CFLAGS+=-O0 -g
	CXXFLAGS+=-O0 -g
else
	CPPFLAGS+=-DNDEBUG
	CFLAGS+=-O2 -g3
	CXXFLAGS+=-O2 -g3
	ifeq ($(STRIP), 1)
		LDFLAGS+=-Wl,--strip-debug
	endif
	ifeq ($(USE_CLANG), 1)
		CFLAGS+=-flto=thin
		CXXFLAGS+=-flto=thin
		LDFLAGS+=-flto=thin
	else
		CFLAGS+=-flto -fuse-linker-plugin
		CXXFLAGS+=-flto -fuse-linker-plugin
		LDFLAGS+=-flto -fuse-linker-plugin
	endif
endif

ifeq ($(HARDENING), 1)
	CPPFLAGS+=-D_FORTIFY_SOURCE=2
	CFLAGS+=-fstack-protector-strong -fstack-clash-protection -fPIE -fvisibility=hidden
	CXXFLAGS+=-fstack-protector-strong -fstack-clash-protection -fPIE -fvisibility=hidden
	LDFLAGS+=-Wl,-z,now -Wl,-z,relro -pie
	ifeq ($(USE_CLANG), 1)
		CFLAGS+=-fsanitize=safe-stack -fsanitize=cfi
		CXXFLAGS+=-fsanitize=safe-stack -fsanitize=cfi
	endif
endif

TARGETS=\
	encryptor-aio \
	replicator-aio \
	encryptor-sgx-aio \
	test-alloc \
	test-xcow \
	test-lbacache \
	xcowsrv \
	xcowdump \
	xcowctl \

OBJECTS=nvme/vm.o nvme/nvme.o nvme/prp.o  crypto/aes_xts_libcrypto.o crypto/aes_xts_ipp.o util/mdev.o util/time.o util/uring.o sgx/enclave.o sgx/Enclave_u.o xcow/xcow_snap.o xcow/xcow_file.o

OBJ_MIMALLOC=
ifneq ($(SANITIZE), 1)
ifeq ($(USE_MIMALLOC), 1)
	CPPFLAGS+=-DUSE_MIMALLOC=1
	ifeq ($(USE_MIMALLOC_DYNAMIC), 1)
	OBJ_MIMALLOC=util/mimalloc_hijack.o
	else
	OBJ_MIMALLOC=$(MIMALLOCROOT)/out/release/mimalloc.o
	endif
endif
endif

DEPS=$(wildcard *.d) $(wildcard crypto/*.d) $(wildcard nvme/*.d) $(wildcard sgx/*.d) $(wildcard util/*.d) $(wildcard xcow/*.d)
JSONS=$(TARGETS:%=%.json)

all: $(TARGETS) writerand compile_commands.json

ifeq ($(USE_CLANG), 1)
CFLAGS+=-MJ $@.json
CXXFLAGS+=-MJ $@.json
compile_commands.json: $(TARGETS) $(OBJECTS)
	sed -e '1s/^/[\n/' -e '$$s/,$$/\n]/' $(JSONS) > $@
else
.PHONY: compile_commands.json
endif

# don't use archive targets, use a single-step archive creation instead
# in order to avoid parallel build issues
libmdevclient.a: $(OBJECTS)
	$(AR) rcs $@ $?

$(TARGETS): %: %.cpp libmdevclient.a $(OBJ_MIMALLOC)
	$(LINK.cpp) $(OBJ_MIMALLOC) $< $(filter-out $(OBJ_MIMALLOC),$(filter %.o,$^)) libmdevclient.a $(LOADLIBES) $(LDLIBS) -o $@

encryptor-mt: LDLIBS+=-l:libippcp.a -lcrypto

encryptor-sgx: LDFLAGS+=-L$(SGX_SDK)/lib64
encryptor-sgx: LDLIBS+=-Wl,--whole-archive -lsgx_uswitchless -Wl,--no-whole-archive -lsgx_urts

encryptor-sgx-aio: LDFLAGS+=-L$(SGX_SDK)/lib64
encryptor-sgx-aio: LDLIBS+=-Wl,--whole-archive -lsgx_uswitchless -Wl,--no-whole-archive -lsgx_urts -luring
encryptor-sgx-aio: nvme/nvme_encryptor_sgx_aio.o

replicator-aio: LDLIBS+=-luring
replicator-aio: nvme/nvme_sender_aio.o

encryptor-aio: LDLIBS+=-l:libippcp.a -lcrypto -luring
encryptor-aio: nvme/nvme_encryptor_aio.o

encryptor-multi: LDLIBS+=-l:libippcp.a -lcrypto -luring
encryptor-multi: nvme/nvme_encryptor_multi.o

#test-xcow: CPPFLAGS+=-DBOOST_STACKTRACE_LINK -DBOOST_STACKTRACE_USE_BACKTRACE
#test-xcow: LDLIBS+=-lboost_stacktrace_backtrace -ldl
test-xcow: xcow/file_deref.o catch_amalgamated.o

test-lbacache: CXXFLAGS+=-O0 -Wno-unused-parameter -Wno-unused-variable -Wno-deprecated-enum-enum-conversion
test-lbacache: LDLIBS+=-lfmt
test-lbacache: catch_amalgamated.o

xcowsrv: LDLIBS+=-luring
#xcowsrv: CPPFLAGS+=-DBOOST_STACKTRACE_LINK -DBOOST_STACKTRACE_USE_BACKTRACE
#xcowsrv: LDLIBS+=-lboost_stacktrace_backtrace -ldl
xcowsrv: xcow/file_deref.o nvme/nvme_xcow.o

xcowdump: LDLIBS+=-lfmt
xcowdump: xcow/file_deref.o

xcowctl: LDLIBS+=-lfmt
xcowctl: xcow/file_deref.o

writerand: LDLIBS=-pthread -l:libippcp.a -lfmt
writerand: writerand.cpp libmdevclient.a
	$(LINK.cpp) $< $(filter-out $(OBJ_MIMALLOC),$(filter %.o,$^)) libmdevclient.a $(LOADLIBES) $(LDLIBS) -o $@

clean:
	$(RM) $(TARGETS)
	$(RM) libmdevclient.a
	$(RM) $(OBJECTS)
	$(RM) $(DEPS)
	$(RM) $(JSONS)
	find . -name '*.[od]' -print -delete

.PHONY: clean

-include $(DEPS)
