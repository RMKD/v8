// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/wasm/wasm-macro-gen.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/test-signatures.h"
#include "test/cctest/wasm/wasm-run-utils.h"

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

#define BUILD(r, ...)                      \
  do {                                     \
    byte code[] = {__VA_ARGS__};           \
    r.Build(code, code + arraysize(code)); \
  } while (false)


#define ADD_CODE(vec, ...)                                              \
  do {                                                                  \
    byte __buf[] = {__VA_ARGS__};                                       \
    for (size_t i = 0; i < sizeof(__buf); i++) vec.push_back(__buf[i]); \
  } while (false)


namespace {
// A helper for generating predictable but unique argument values that
// are easy to debug (e.g. with misaligned stacks).
class PredictableInputValues {
 public:
  int base_;
  explicit PredictableInputValues(int base) : base_(base) {}
  double arg_d(int which) { return base_ * which + ((which & 1) * 0.5); }
  float arg_f(int which) { return base_ * which + ((which & 1) * 0.25); }
  int32_t arg_i(int which) { return base_ * which + ((which & 1) * kMinInt); }
  int64_t arg_l(int which) {
    return base_ * which + ((which & 1) * (0x04030201LL << 32));
  }
};


uint32_t AddJsFunction(TestingModule* module, FunctionSig* sig,
                       const char* source) {
  Handle<JSFunction> jsfunc = Handle<JSFunction>::cast(v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(CompileRun(source))));
  module->AddFunction(sig, Handle<Code>::null());
  uint32_t index = static_cast<uint32_t>(module->module->functions.size() - 1);
  Isolate* isolate = CcTest::InitIsolateOnce();
  Handle<Code> code =
      CompileWasmToJSWrapper(isolate, module, jsfunc, sig, "test", nullptr);
  module->instance->function_code[index] = code;
  return index;
}


uint32_t AddJSSelector(TestingModule* module, FunctionSig* sig, int which) {
  const int kMaxParams = 8;
  static const char* formals[kMaxParams] = {
      "",        "a",         "a,b",         "a,b,c",
      "a,b,c,d", "a,b,c,d,e", "a,b,c,d,e,f", "a,b,c,d,e,f,g",
  };
  CHECK_LT(which, static_cast<int>(sig->parameter_count()));
  CHECK_LT(static_cast<int>(sig->parameter_count()), kMaxParams);

  i::EmbeddedVector<char, 256> source;
  char param = 'a' + which;
  SNPrintF(source, "(function(%s) { return %c; })",
           formals[sig->parameter_count()], param);

  return AddJsFunction(module, sig, source.start());
}


Handle<JSFunction> WrapCode(ModuleEnv* module, uint32_t index) {
  Isolate* isolate = module->module->shared_isolate;
  // Wrap the code so it can be called as a JS function.
  Handle<String> name = isolate->factory()->NewStringFromStaticChars("main");
  Handle<JSObject> module_object = Handle<JSObject>(0, isolate);
  Handle<Code> code = module->instance->function_code[index];
  WasmJs::InstallWasmFunctionMap(isolate, isolate->native_context());
  return compiler::CompileJSToWasmWrapper(isolate, module, name, code,
                                          module_object, index);
}


void EXPECT_CALL(double expected, Handle<JSFunction> jsfunc,
                 Handle<Object>* buffer, int count) {
  Isolate* isolate = jsfunc->GetIsolate();
  Handle<Object> global(isolate->context()->global_object(), isolate);
  MaybeHandle<Object> retval =
      Execution::Call(isolate, jsfunc, global, count, buffer);

  CHECK(!retval.is_null());
  Handle<Object> result = retval.ToHandleChecked();
  if (result->IsSmi()) {
    CHECK_EQ(expected, Smi::cast(*result)->value());
  } else {
    CHECK(result->IsHeapNumber());
    CheckFloatEq(expected, HeapNumber::cast(*result)->value());
  }
}


void EXPECT_CALL(double expected, Handle<JSFunction> jsfunc, double a,
                 double b) {
  Isolate* isolate = jsfunc->GetIsolate();
  Handle<Object> buffer[] = {isolate->factory()->NewNumber(a),
                             isolate->factory()->NewNumber(b)};
  EXPECT_CALL(expected, jsfunc, buffer, 2);
}
}  // namespace

TEST(Run_Int32Sub_jswrapped) {
  TestSignatures sigs;
  TestingModule module;
  WasmFunctionCompiler t(sigs.i_ii(), &module);
  BUILD(t, WASM_I32_SUB(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());

  EXPECT_CALL(33, jsfunc, 44, 11);
  EXPECT_CALL(-8723487, jsfunc, -8000000, 723487);
}


TEST(Run_Float32Div_jswrapped) {
  TestSignatures sigs;
  TestingModule module;
  WasmFunctionCompiler t(sigs.f_ff(), &module);
  BUILD(t, WASM_F32_DIV(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());

  EXPECT_CALL(92, jsfunc, 46, 0.5);
  EXPECT_CALL(64, jsfunc, -16, -0.25);
}


TEST(Run_Float64Add_jswrapped) {
  TestSignatures sigs;
  TestingModule module;
  WasmFunctionCompiler t(sigs.d_dd(), &module);
  BUILD(t, WASM_F64_ADD(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());

  EXPECT_CALL(3, jsfunc, 2, 1);
  EXPECT_CALL(-5.5, jsfunc, -5.25, -0.25);
}


TEST(Run_I32Popcount_jswrapped) {
  TestSignatures sigs;
  TestingModule module;
  WasmFunctionCompiler t(sigs.i_i(), &module);
  BUILD(t, WASM_I32_POPCNT(WASM_GET_LOCAL(0)));
  Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());

  EXPECT_CALL(2, jsfunc, 9, 0);
  EXPECT_CALL(3, jsfunc, 11, 0);
  EXPECT_CALL(6, jsfunc, 0x3F, 0);

  USE(AddJsFunction);
}


#if !V8_TARGET_ARCH_ARM64
// TODO(titzer): dynamic frame alignment on arm64
TEST(Run_CallJS_Add_jswrapped) {
  TestSignatures sigs;
  TestingModule module;
  WasmFunctionCompiler t(sigs.i_i(), &module);
  uint32_t js_index =
      AddJsFunction(&module, sigs.i_i(), "(function(a) { return a + 99; })");
  BUILD(t, WASM_CALL_FUNCTION(js_index, WASM_GET_LOCAL(0)));

  Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());

  EXPECT_CALL(101, jsfunc, 2, -8);
  EXPECT_CALL(199, jsfunc, 100, -1);
  EXPECT_CALL(-666666801, jsfunc, -666666900, -1);
}
#endif


void RunJSSelectTest(int which) {
#if !V8_TARGET_ARCH_ARM
  // TODO(titzer): fix tests on arm and reenable
  const int kMaxParams = 8;
  PredictableInputValues inputs(0x100);
  LocalType type = kAstF64;
  LocalType types[kMaxParams + 1] = {type, type, type, type, type,
                                     type, type, type, type};
  for (int num_params = which + 1; num_params < kMaxParams; num_params++) {
    HandleScope scope(CcTest::InitIsolateOnce());
    FunctionSig sig(1, num_params, types);

    TestingModule module;
    uint32_t js_index = AddJSSelector(&module, &sig, which);
    WasmFunctionCompiler t(&sig, &module);

    {
      std::vector<byte> code;
      ADD_CODE(code, kExprCallFunction, static_cast<byte>(js_index));

      for (int i = 0; i < num_params; i++) {
        ADD_CODE(code, WASM_F64(inputs.arg_d(i)));
      }

      size_t end = code.size();
      code.push_back(0);
      t.Build(&code[0], &code[end]);
    }

    Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());
    double expected = inputs.arg_d(which);
    EXPECT_CALL(expected, jsfunc, 0.0, 0.0);
  }
#endif
}


TEST(Run_JSSelect_0) { RunJSSelectTest(0); }

TEST(Run_JSSelect_1) { RunJSSelectTest(1); }

TEST(Run_JSSelect_2) { RunJSSelectTest(2); }

TEST(Run_JSSelect_3) { RunJSSelectTest(3); }

TEST(Run_JSSelect_4) { RunJSSelectTest(4); }

TEST(Run_JSSelect_5) { RunJSSelectTest(5); }

TEST(Run_JSSelect_6) { RunJSSelectTest(6); }

TEST(Run_JSSelect_7) { RunJSSelectTest(7); }


void RunWASMSelectTest(int which) {
  PredictableInputValues inputs(0x200);
  Isolate* isolate = CcTest::InitIsolateOnce();
  const int kMaxParams = 8;
  for (int num_params = which + 1; num_params < kMaxParams; num_params++) {
    LocalType type = kAstF64;
    LocalType types[kMaxParams + 1] = {type, type, type, type, type,
                                       type, type, type, type};
    FunctionSig sig(1, num_params, types);

    TestingModule module;
    WasmFunctionCompiler t(&sig, &module);
    BUILD(t, WASM_GET_LOCAL(which));
    Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());

    Handle<Object> args[] = {
        isolate->factory()->NewNumber(inputs.arg_d(0)),
        isolate->factory()->NewNumber(inputs.arg_d(1)),
        isolate->factory()->NewNumber(inputs.arg_d(2)),
        isolate->factory()->NewNumber(inputs.arg_d(3)),
        isolate->factory()->NewNumber(inputs.arg_d(4)),
        isolate->factory()->NewNumber(inputs.arg_d(5)),
        isolate->factory()->NewNumber(inputs.arg_d(6)),
        isolate->factory()->NewNumber(inputs.arg_d(7)),
    };

    double expected = inputs.arg_d(which);
    EXPECT_CALL(expected, jsfunc, args, kMaxParams);
  }
}


TEST(Run_WASMSelect_0) { RunWASMSelectTest(0); }

TEST(Run_WASMSelect_1) { RunWASMSelectTest(1); }

TEST(Run_WASMSelect_2) { RunWASMSelectTest(2); }

TEST(Run_WASMSelect_3) { RunWASMSelectTest(3); }

TEST(Run_WASMSelect_4) { RunWASMSelectTest(4); }

TEST(Run_WASMSelect_5) { RunWASMSelectTest(5); }

TEST(Run_WASMSelect_6) { RunWASMSelectTest(6); }

TEST(Run_WASMSelect_7) { RunWASMSelectTest(7); }


void RunWASMSelectAlignTest(int num_args, int num_params) {
  PredictableInputValues inputs(0x300);
  Isolate* isolate = CcTest::InitIsolateOnce();
  const int kMaxParams = 4;
  DCHECK_LE(num_args, kMaxParams);
  LocalType type = kAstF64;
  LocalType types[kMaxParams + 1] = {type, type, type, type, type};
  FunctionSig sig(1, num_params, types);

  for (int which = 0; which < num_params; which++) {
    TestingModule module;
    WasmFunctionCompiler t(&sig, &module);
    BUILD(t, WASM_GET_LOCAL(which));
    Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());

    Handle<Object> args[] = {
        isolate->factory()->NewNumber(inputs.arg_d(0)),
        isolate->factory()->NewNumber(inputs.arg_d(1)),
        isolate->factory()->NewNumber(inputs.arg_d(2)),
        isolate->factory()->NewNumber(inputs.arg_d(3)),
    };

    double nan = std::numeric_limits<double>::quiet_NaN();
    double expected = which < num_args ? inputs.arg_d(which) : nan;
    EXPECT_CALL(expected, jsfunc, args, num_args);
  }
}


TEST(Run_WASMSelectAlign_0) {
  RunWASMSelectAlignTest(0, 1);
  RunWASMSelectAlignTest(0, 2);
}


TEST(Run_WASMSelectAlign_1) {
  RunWASMSelectAlignTest(1, 2);
  RunWASMSelectAlignTest(1, 3);
}


TEST(Run_WASMSelectAlign_2) {
  RunWASMSelectAlignTest(2, 3);
  RunWASMSelectAlignTest(2, 4);
}


TEST(Run_WASMSelectAlign_3) {
  RunWASMSelectAlignTest(3, 3);
  RunWASMSelectAlignTest(3, 4);
}


TEST(Run_WASMSelectAlign_4) {
  RunWASMSelectAlignTest(4, 3);
  RunWASMSelectAlignTest(4, 4);
}


void RunJSSelectAlignTest(int num_args, int num_params) {
  PredictableInputValues inputs(0x400);
  Isolate* isolate = CcTest::InitIsolateOnce();
  Factory* factory = isolate->factory();
  const int kMaxParams = 4;
  CHECK_LE(num_args, kMaxParams);
  CHECK_LE(num_params, kMaxParams);
  LocalType type = kAstF64;
  LocalType types[kMaxParams + 1] = {type, type, type, type, type};
  FunctionSig sig(1, num_params, types);

  // Build the calling code.
  std::vector<byte> code;
  ADD_CODE(code, kExprCallFunction, 0);

  for (int i = 0; i < num_params; i++) {
    ADD_CODE(code, WASM_GET_LOCAL(i));
  }

  size_t end = code.size();
  code.push_back(0);

  // Call different select JS functions.
  for (int which = 0; which < num_params; which++) {
    HandleScope scope(isolate);
    TestingModule module;
    uint32_t js_index = AddJSSelector(&module, &sig, which);
    CHECK_EQ(0, js_index);
    WasmFunctionCompiler t(&sig, &module);
    t.Build(&code[0], &code[end]);

    Handle<JSFunction> jsfunc = WrapCode(&module, t.CompileAndAdd());

    Handle<Object> args[] = {
        factory->NewNumber(inputs.arg_d(0)),
        factory->NewNumber(inputs.arg_d(1)),
        factory->NewNumber(inputs.arg_d(2)),
        factory->NewNumber(inputs.arg_d(3)),
    };

    double nan = std::numeric_limits<double>::quiet_NaN();
    double expected = which < num_args ? inputs.arg_d(which) : nan;
    EXPECT_CALL(expected, jsfunc, args, num_args);
  }
}


TEST(Run_JSSelectAlign_0) {
  RunJSSelectAlignTest(0, 1);
  RunJSSelectAlignTest(0, 2);
}


TEST(Run_JSSelectAlign_2) {
  RunJSSelectAlignTest(2, 3);
  RunJSSelectAlignTest(2, 4);
}


TEST(Run_JSSelectAlign_4) {
  RunJSSelectAlignTest(4, 3);
  RunJSSelectAlignTest(4, 4);
}


#if !V8_TARGET_ARCH_ARM64
// TODO(titzer): dynamic frame alignment on arm64
TEST(Run_JSSelectAlign_1) {
  RunJSSelectAlignTest(1, 2);
  RunJSSelectAlignTest(1, 3);
}


TEST(Run_JSSelectAlign_3) {
  RunJSSelectAlignTest(3, 3);
  RunJSSelectAlignTest(3, 4);
}
#endif
