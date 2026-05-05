#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <CoreFoundation/CoreFoundation.h>

// CFTypeRef → Lua
static void cf_to_lua(lua_State *L, CFTypeRef ref) {
    if (!ref) { lua_pushnil(L); return; }

    CFTypeID type = CFGetTypeID(ref);

    if (type == CFDictionaryGetTypeID()) {
        CFDictionaryRef dict = (CFDictionaryRef)ref;
        lua_newtable(L);
        CFIndex count = CFDictionaryGetCount(dict);
        CFTypeRef *keys = malloc(count * sizeof(CFTypeRef));
        CFTypeRef *values = malloc(count * sizeof(CFTypeRef));
        CFDictionaryGetKeysAndValues(dict, keys, values);
        for (CFIndex i = 0; i < count; i++) {
            cf_to_lua(L, keys[i]);
            cf_to_lua(L, values[i]);
            lua_settable(L, -3);
        }
        free(keys); free(values);
    }
    else if (type == CFArrayGetTypeID()) {
        CFArrayRef arr = (CFArrayRef)ref;
        CFIndex count = CFArrayGetCount(arr);
        lua_newtable(L);
        for (CFIndex i = 0; i < count; i++) {
            lua_pushinteger(L, i + 1);
            cf_to_lua(L, CFArrayGetValueAtIndex(arr, i));
            lua_settable(L, -3);
        }
    }
    else if (type == CFStringGetTypeID()) {
        CFStringRef str = (CFStringRef)ref;
        CFIndex len = CFStringGetLength(str) * 3 + 1;
        char *buf = malloc(len);
        if (CFStringGetCString(str, buf, len, kCFStringEncodingUTF8)) {
            lua_pushstring(L, buf);
        } else {
            lua_pushstring(L, "");
        }
        free(buf);
    }
    else if (type == CFNumberGetTypeID()) {
        CFNumberRef num = (CFNumberRef)ref;
        if (CFNumberIsFloatType(num)) {
            double d; CFNumberGetValue(num, kCFNumberDoubleType, &d);
            lua_pushnumber(L, d);
        } else {
            long long l; CFNumberGetValue(num, kCFNumberSInt64Type, &l);
            lua_pushnumber(L, (lua_Number)l);
        }
    }
    else if (type == CFBooleanGetTypeID()) {
        lua_pushboolean(L, CFBooleanGetValue((CFBooleanRef)ref));
    }
    else if (type == CFDataGetTypeID()) {
        CFDataRef data = (CFDataRef)ref;
        lua_pushlstring(L, (const char *)CFDataGetBytePtr(data), CFDataGetLength(data));
    }
    else if (type == CFDateGetTypeID()) {
        CFDateRef date = (CFDateRef)ref;
        lua_pushnumber(L, CFDateGetAbsoluteTime(date) + kCFAbsoluteTimeIntervalSince1970);
    }
    else {
        lua_pushnil(L);
    }
}

// Lua table → CFTypeRef
static CFTypeRef lua_to_cf(lua_State *L, int idx) {
    int type = lua_type(L, idx);

    switch (type) {
        case LUA_TTABLE: {
            lua_pushnil(L);
            if (!lua_next(L, idx - 1)) {
                lua_pop(L, 1);
                return CFDictionaryCreateMutable(NULL, 0,
                    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            }
            lua_pop(L, 2);
            int keyType = lua_type(L, -1);

            if (keyType == LUA_TNUMBER) {
                CFMutableArrayRef arr = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
                int i = 1;
                while (1) {
                    lua_pushinteger(L, i++);
                    lua_gettable(L, idx - 1);
                    if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
                    CFTypeRef val = lua_to_cf(L, lua_gettop(L));
                    if (val) { CFArrayAppendValue(arr, val); CFRelease(val); }
                    lua_pop(L, 1);
                }
                return arr;
            } else {
                CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0,
                    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
                lua_pushnil(L);
                while (lua_next(L, idx - 1)) {
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        const char *k = lua_tostring(L, -2);
                        CFStringRef key = CFStringCreateWithCString(NULL, k, kCFStringEncodingUTF8);
                        CFTypeRef val = lua_to_cf(L, lua_gettop(L));
                        if (key && val) {
                            CFDictionarySetValue(dict, key, val);
                            CFRelease(key);
                            CFRelease(val);
                        } else {
                            if (key) CFRelease(key);
                            if (val) CFRelease(val);
                        }
                    }
                    lua_pop(L, 1);
                }
                return dict;
            }
        }
        case LUA_TSTRING: {
            return CFStringCreateWithCString(NULL, lua_tostring(L, idx), kCFStringEncodingUTF8);
        }
        case LUA_TNUMBER: {
            double n = lua_tonumber(L, idx);
            return CFNumberCreate(NULL, kCFNumberDoubleType, &n);
        }
        case LUA_TBOOLEAN: {
            return CFBooleanCreate(NULL, lua_toboolean(L, idx));
        }
        default:
            return CFRetain(kCFNull);
    }
}

// plist.read(path) → table
static int l_plist_read(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);

    CFStringRef pathStr = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
    CFURLRef fileURL = CFURLCreateWithFileSystemPath(NULL, pathStr, kCFURLPOSIXPathStyle, false);
    CFRelease(pathStr);
    if (!fileURL) { lua_pushnil(L); return 1; }

    CFReadStreamRef stream = CFReadStreamCreateWithFile(NULL, fileURL);
    CFRelease(fileURL);
    if (!stream || !CFReadStreamOpen(stream)) {
        if (stream) CFRelease(stream);
        lua_pushnil(L);
        return 1;
    }

    CFMutableDataRef data = CFDataCreateMutable(NULL, 0);
    UInt8 buf[4096];
    CFIndex n;
    while ((n = CFReadStreamRead(stream, buf, 4096)) > 0) CFDataAppendBytes(data, buf, n);
    CFReadStreamClose(stream);
    CFRelease(stream);

    CFErrorRef err = NULL;
    CFPropertyListRef plist = CFPropertyListCreateWithData(NULL, data,
        kCFPropertyListImmutable, NULL, &err);
    CFRelease(data);

    if (err || !plist) {
        if (err) CFRelease(err);
        lua_pushnil(L);
        return 1;
    }

    cf_to_lua(L, plist);
    CFRelease(plist);
    return 1;
}

// plist.write(path, table) → bool
static int l_plist_write(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    CFTypeRef plist = lua_to_cf(L, 2);
    if (!plist) { lua_pushboolean(L, 0); return 1; }

    CFErrorRef err = NULL;
    CFDataRef xmlData = CFPropertyListCreateData(NULL, plist,
        kCFPropertyListXMLFormat_v1_0, 0, &err);
    CFRelease(plist);
    if (err || !xmlData) {
        if (err) CFRelease(err);
        lua_pushboolean(L, 0);
        return 1;
    }

    CFStringRef pathStr = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
    CFURLRef fileURL = CFURLCreateWithFileSystemPath(NULL, pathStr, kCFURLPOSIXPathStyle, false);
    CFRelease(pathStr);

    CFWriteStreamRef stream = CFWriteStreamCreateWithFile(NULL, fileURL);
    CFRelease(fileURL);

    int ok = 0;
    if (stream && CFWriteStreamOpen(stream)) {
        CFIndex written = CFWriteStreamWrite(stream,
            CFDataGetBytePtr(xmlData), CFDataGetLength(xmlData));
        ok = (written == CFDataGetLength(xmlData)) ? 1 : 0;
        CFWriteStreamClose(stream);
        CFRelease(stream);
    } else {
        if (stream) CFRelease(stream);
    }
    CFRelease(xmlData);

    lua_pushboolean(L, ok);
    return 1;
}

static const luaL_Reg plist_funcs[] = {
    {"read",  l_plist_read},
    {"write", l_plist_write},
    {NULL, NULL}
};

// require("sz") → { plist = { read, write } }
int luaopen_sz(lua_State *L) {
    lua_newtable(L);
    lua_newtable(L);
    luaL_setfuncs(L, plist_funcs, 0);
    lua_setfield(L, -2, "plist");
    return 1;
}