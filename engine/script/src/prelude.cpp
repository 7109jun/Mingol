// SPDX-License-Identifier: GPL-2.0-only
// The language prelude. Generated text is checked in; keep it in sync with the VM's expectations
// (field order of Exception: message = 0, stackTrace = 1; native method names).
#include "mingol/script/compile.hpp"

namespace mingol::script {

const char* preludeSource() noexcept {
  return R"MINS(module mingol;

public class Object {
}

public class Exception {
    public string message;
    public string stackTrace;

    public Exception() {
    }

    public Exception(string message) {
        this.message = message;
    }

    public string getMessage() {
        return message;
    }

    public string getStackTrace() {
        return stackTrace;
    }
}

public class RuntimeError extends Exception {
    public RuntimeError() {
    }

    public RuntimeError(string message) {
        super(message);
    }
}

public class NullReferenceError extends RuntimeError {
    public NullReferenceError() {
    }

    public NullReferenceError(string message) {
        super(message);
    }
}

public class IndexOutOfRangeError extends RuntimeError {
    public IndexOutOfRangeError() {
    }

    public IndexOutOfRangeError(string message) {
        super(message);
    }
}

public class DivideByZeroError extends RuntimeError {
    public DivideByZeroError() {
    }

    public DivideByZeroError(string message) {
        super(message);
    }
}

public class CastError extends RuntimeError {
    public CastError() {
    }

    public CastError(string message) {
        super(message);
    }
}

public class StackOverflowError extends RuntimeError {
    public StackOverflowError() {
    }

    public StackOverflowError(string message) {
        super(message);
    }
}

public class OutOfMemoryError extends RuntimeError {
    public OutOfMemoryError() {
    }

    public OutOfMemoryError(string message) {
        super(message);
    }
}

public class ArgumentError extends RuntimeError {
    public ArgumentError() {
    }

    public ArgumentError(string message) {
        super(message);
    }
}

public class NativeError extends RuntimeError {
    public NativeError() {
    }

    public NativeError(string message) {
        super(message);
    }
}

public native class Coroutine {
    private Coroutine() {
    }

    public native bool isDone();
    public native void resume();
    public native int yieldKind();
    public native int yieldedInt();
    public native float yieldedFloat();
    public native bool yieldedBool();
    public native string yieldedString();
    public native Object yieldedObject();
}

public native class Type {
    private Type() {
    }

    public native string getName();
    public native Type getBase();
    public native bool isClass();
    public native bool isInterface();
    public native bool isEnum();
    public native bool isAbstract();
    public native bool isAssignableFrom(Type other);
    public native FieldInfo[] getFields();
    public native MethodInfo[] getMethods();
    public native FieldInfo getField(string name);
    public native MethodInfo getMethod(string name);
    public native Object newInstance();
    public native static Type forName(string qualifiedName);
}

public native class FieldInfo {
    private FieldInfo() {
    }

    public native string getName();
    public native string getTypeName();
    public native bool isStatic();
    public native int getInt(Object target);
    public native float getFloat(Object target);
    public native bool getBool(Object target);
    public native string getString(Object target);
    public native Object getObject(Object target);
    public native void setInt(Object target, int value);
    public native void setFloat(Object target, float value);
    public native void setBool(Object target, bool value);
    public native void setString(Object target, string value);
    public native void setObject(Object target, Object value);
}

public native class MethodInfo {
    private MethodInfo() {
    }

    public native string getName();
    public native string getReturnTypeName();
    public native int getParameterCount();
    public native bool isStatic();
    public native Object invoke(Object target, Object[] args);
}

public class IntBox {
    public int value;

    public IntBox(int value) {
        this.value = value;
    }
}

public class FloatBox {
    public float value;

    public FloatBox(float value) {
        this.value = value;
    }
}

public class BoolBox {
    public bool value;

    public BoolBox(bool value) {
        this.value = value;
    }
}

public class StringBox {
    public string value;

    public StringBox(string value) {
        this.value = value;
    }
}
)MINS";
}

}  // namespace mingol::script
