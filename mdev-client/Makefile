# apt install llvm-12-dev clang-12 lld-12 libc++-12-dev libc++abi-12-dev libssl-dev libboost-all-dev intel-oneapi-ippcp-devel

USE_CLANG?=1
USE_LIBCXX?=0

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
	CC=gcc-10
	CXX=g++-10
	AR=gcc-ar-10
endif

CPPFLAGS+=-D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -MMD -MP
CPPFLAGS+=-I/lib/modules/$(shell uname -r)/build/usr/include -Iinclude
CFLAGS+=-Wall -Wextra -Wformat=2 -std=c11 -fwrapv
CXXFLAGS+=-Wall -Wextra -Wformat=2 -std=c++20 -fwrapv

IPPCRYPTOROOT?=/opt/intel/oneapi/ippcp/latest
IPPCP_TARGET_ARCH?=intel64
LIBRARY_PATH?=$(IPPCRYPTOROOT)/lib/$(IPPCP_TARGET_ARCH)

CPPFLAGS+=-pthread -I$(IPPCRYPTOROOT)/include
LDFLAGS+=-L$(LIBRARY_PATH)
LDLIBS+=-pthread

SGX_SDK?=/opt/intel/sgxsdk
CPPFLAGS+=-I$(SGX_SDK)/include -Iinclude/sgx

ifeq ($(DEBUG), 1)
	CPPFLAGS+=-DDEBUG=1
	CFLAGS+=-O0 -g3 -fno-omit-frame-pointer
	CXXFLAGS+=-O0 -g3 -fno-omit-frame-pointer
	ifeq ($(USE_LIBCXX), 1)
		CPPFLAGS+=-D_LIBCPP_DEBUG=1
	else
		CPPFLAGS+=-D_GLIBCXX_DEBUG
	endif
else ifeq ($(PROFILE), 1)
	CPPFLAGS+=-DNDEBUG -DPROFILE=1
	CFLAGS+=-O2 -g
	CXXFLAGS+=-O2 -g
	LDLIBS+=-lprofiler
else ifdef SANITIZE
	CPPFLAGS+=-DNDEBUG
	CFLAGS+=-Og -g3 -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
	CXXFLAGS+=-Og -g3 -fsanitize=$(SANITIZE) -fno-omit-frame-pointer
else
	CPPFLAGS+=-DNDEBUG
	CFLAGS+=-O2
	CXXFLAGS+=-O2
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
	encryptor-multi \

OBJECTS=nvme/vm.o nvme/nvme.o nvme/prp.o  crypto/aes_xts_libcrypto.o crypto/aes_xts_ipp.o util/mdev.o util/time.o util/uring.o sgx/enclave.o sgx/Enclave_u.o

DEPS=$(TARGETS:%=%.d) $(OBJECTS:%.o=%.d)
JSONS=$(TARGETS:%=%.json) $(OBJECTS:%=%.json)

all: $(TARGETS) compile_commands.json

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

%: %.cpp libmdevclient.a
	$(LINK.cpp) $< $(filter %.o,$^) libmdevclient.a $(LOADLIBES) $(LDLIBS) -o $@

encryptor-mt: LDLIBS+=-l:libippcp.a -lcrypto

encryptor-sgx: LDFLAGS+=-L$(SGX_SDK)/lib64
encryptor-sgx: LDLIBS+=-Wl,--whole-archive -lsgx_uswitchless -Wl,--no-whole-archive -lsgx_urts

encryptor-sgx-aio: LDFLAGS+=-L$(SGX_SDK)/lib64 -luring
encryptor-sgx-aio: LDLIBS+=-Wl,--whole-archive -lsgx_uswitchless -Wl,--no-whole-archive -lsgx_urts
encryptor-sgx-aio: nvme/nvme_encryptor_sgx_aio.o

replicator-aio: LDLIBS+=-luring
replicator-aio: nvme/nvme_sender_aio.o

encryptor-aio: LDLIBS+=-l:libippcp.a -lcrypto -luring
encryptor-aio: nvme/nvme_encryptor_aio.o

encryptor-multi: LDLIBS+=-l:libippcp.a -lcrypto -luring
encryptor-multi: nvme/nvme_encryptor_multi.o

clean:
	$(RM) $(TARGETS)
	$(RM) compile_commands.json libmdevclient.a
	$(RM) $(OBJECTS)
	$(RM) $(DEPS)
	$(RM) $(JSONS)

.PHONY: clean

-include $(DEPS)
