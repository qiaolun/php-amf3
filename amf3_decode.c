/*
 ** Copyright (C) 2010, 2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
 ** Please read the LICENSE file for license details
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_amf3.h"
#include "Zend/zend_interfaces.h"
#include "amf3.h"


typedef struct {
    int fmt, cnt;
    zend_string *cls;
    const char **fld;
    int *flen;
} Traits;

static void storeRef(zval *val, HashTable *ht) {
    zval hv;
    ZVAL_NEW_REF(&hv, val);
    Z_TRY_ADDREF_P(val);
    zend_hash_next_index_insert(ht, &hv);
}

static int decodeValue(zval *val, const char *buf, int pos, int size, int opts, HashTable *sht, HashTable *oht, HashTable *tht);

static int decodeU29(int *val, const char *buf, int pos, int size) {
    int ofs = 0, res = 0, tmp;
    buf += pos;
    do {
        if ((pos + ofs) >= size) {
            php_error(E_WARNING, "Insufficient integer data at position %d", pos);
            return -1;
        }
        tmp = buf[ofs];
        if (ofs == 3) {
            res <<= 8;
            res |= tmp & 0xff;
        } else {
            res <<= 7;
            res |= tmp & 0x7f;
        }
    } while ((++ofs < 4) && (tmp & 0x80));
    *val = res;
    return ofs;
}

static int decodeDouble(zval *val, const char *buf, int pos, int size) {
    union { int i; char c; } t;
    union { double d; char c[8]; } u;
    if ((pos + 8) > size) {
        php_error(E_WARNING, "Insufficient number data at position %d", pos);
        return -1;
    }
    buf += pos;
    t.i = 1;
    if (!t.c) memcpy(u.c, buf, 8);
    else { /* little-endian machine */
        int i;
        for (i = 0; i < 8; ++i) u.c[i] = buf[7 - i];
    }
    ZVAL_DOUBLE(val, u.d);
    return 8;
}

static int decodeStr(const char **str, int *len, zval *val, const char *buf, int pos, int size, int loose, HashTable *ht) {
    int old = pos, ofs, pfx, def;
    ofs = decodeU29(&pfx, buf, pos, size);
    if (ofs < 0) return -1;
    pos += ofs;
    def = pfx & 1;
    pfx >>= 1;
    if (def) {
        if ((pos + pfx) > size) {
            php_error(E_WARNING, "Insufficient data of length %d at position %d", pfx, pos);
            return -1;
        }
        buf += pos;
        pos += pfx;
        if (val) {
            ZVAL_STRINGL(val, buf, pfx);
        } else {
            *str = buf;
            *len = pfx;
        }
        if (loose || pfx) { /* empty string is never sent by reference */
            zval hv;
            if (val) {
                ZVAL_COPY(&hv, val);
            } else {
                ZVAL_STRINGL(&hv, buf, pfx);
            }
            zend_hash_next_index_insert(ht, &hv);
        }
    } else {
        zval *hv;
        if (!(hv = zend_hash_index_find(ht, pfx))) {
            php_error(E_WARNING, "Missing string reference #%d at position %d", pfx, pos);
            return -1;
        }
        if (val) {
            ZVAL_COPY(val, hv);
        } else {
            *str = Z_STRVAL_P(hv);
            *len = Z_STRLEN_P(hv);
        }
    }
    return pos - old;
}

static int decodeRef(int *len, zval *val, const char *buf, int pos, int size, HashTable *ht) {
    int ofs, pfx, def;
    ofs = decodeU29(&pfx, buf, pos, size);
    if (ofs < 0) return -1;
    pos += ofs;
    def = pfx & 1;
    pfx >>= 1;
    if (def) *len = pfx;
    else {
        zval *hv;
        if (!(hv = zend_hash_index_find(ht, pfx))) {
            php_error(E_WARNING, "Missing object reference #%d at position %d", pfx, pos);
            return -1;
        }
        *len = -1;
        ZVAL_COPY_OR_DUP(val, hv);
        SEPARATE_ZVAL(val);
    }
    return ofs;
}

static int decodeDate(zval *val, const char *buf, int pos, int size, HashTable *ht) {
    int old = pos, ofs, pfx;
    ofs = decodeRef(&pfx, val, buf, pos, size, ht);
    if (ofs < 0) return -1;
    pos += ofs;
    if (pfx >= 0) {
        ofs = decodeDouble(val, buf, pos, size);
        if (ofs < 0) return -1;
        pos += ofs;
        storeRef(val, ht);
    }
    return pos - old;
}

static zval *newIdx(zval *val) {
    zval hv;
    ZVAL_UNDEF(&hv);
    HT_ALLOW_COW_VIOLATION(HASH_OF(val));
    return zend_hash_next_index_insert(HASH_OF(val), &hv);
}

static zval *newKey(zval *val, const char *key, int len) {
    zval hv;
    ZVAL_UNDEF(&hv);
    HT_ALLOW_COW_VIOLATION(HASH_OF(val));
    return zend_symtable_str_update(HASH_OF(val), key, len, &hv);
}

static int decodeArray(zval *val, const char *buf, int pos, int size, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
    int old = pos, ofs, len;
    ofs = decodeRef(&len, val, buf, pos, size, oht);
    if (ofs < 0) return -1;
    pos += ofs;
    if (len >= 0) {
        const char *key;
        int klen;
        array_init(val);
        storeRef(val, oht);
        for ( ;; ) { /* associative portion */
            ofs = decodeStr(&key, &klen, 0, buf, pos, size, 0, sht);
            if (ofs < 0) return -1;
            pos += ofs;
            if (!klen) break;
            ofs = decodeValue(newKey(val, key, klen), buf, pos, size, opts, sht, oht, tht);
            if (ofs < 0) return -1;
            pos += ofs;
        }
        while (len--) { /* dense portion */
            ofs = decodeValue(newIdx(val), buf, pos, size, opts, sht, oht, tht);
            if (ofs < 0) return -1;
            pos += ofs;
        }
    }
    return pos - old;
}

static int decodeObject(zval *val, const char *buf, int pos, int size, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
    int old = pos, ofs, pfx;
    ofs = decodeRef(&pfx, val, buf, pos, size, oht);
    if (ofs < 0) return -1;
    pos += ofs;
    if (pfx >= 0) {
        int map = opts & AMF3_CLASS_MAP;
        zend_class_entry *ce = 0;
        Traits *tr;
        const char *key;
        int klen;
        int def = pfx & 1;
        pfx >>= 1;
        if (def) { /* new class definition */
            int i, n = pfx >> 2;
            const char *cls;
            int clen;
            const char **fld = 0;
            int *flen = 0;
            ofs = decodeStr(&cls, &clen, 0, buf, pos, size, 0, sht);
            if (ofs < 0) return -1;
            pos += ofs;
            if (n > 0) {
                if ((pos + n) > size) { /* rough security check */
                    php_error(E_WARNING, "Inappropriate number of declared class members at position %d", pos);
                    return -1;
                }
                fld = emalloc(n * sizeof *fld);
                flen = emalloc(n * sizeof *flen);
                for (i = 0; i < n; ++i) { /* static member names */
                    ofs = decodeStr(&key, &klen, 0, buf, pos, size, 0, sht);
                    if (ofs < 0) {
                        n = -1;
                        break;
                    }
                    pos += ofs;
                    if (!klen || !key[0]) {
                        php_error(E_WARNING, "Inappropriate class member name at position %d", pos);
                        n = -1;
                        break;
                    }
                    fld[i] = key;
                    flen[i] = klen;
                }
                if (n < 0) {
                    efree(fld);
                    efree(flen);
                    return -1;
                }
            }
            tr = emalloc(sizeof *tr);
            tr->fmt = pfx & 3;
            tr->cnt = n;
            tr->cls = clen ? zend_string_init(cls, clen, 0) : 0;
            tr->fld = fld;
            tr->flen = flen;
            zend_hash_next_index_insert_ptr(tht, tr);
        } else if (!(tr = zend_hash_index_find_ptr(tht, pfx))) { /* Existing class definition */
            php_error(E_WARNING, "Invalid class reference %d at position %d", pfx, old);
            return -1;
        }
        if (!tr->cls) {
            map = 1;
        }

        if (!map) array_init(val);
        else {
            if (!tr->cls) object_init(val);
            else {
                int mode = ZEND_FETCH_CLASS_DEFAULT | ZEND_FETCH_CLASS_SILENT;
                if (!(opts & AMF3_CLASS_AUTOLOAD)) mode |= ZEND_FETCH_CLASS_NO_AUTOLOAD;
                ce = zend_fetch_class(tr->cls, mode);
                if (!ce) {
                    php_error(E_WARNING, "Class '%s' not found at position %d", ZSTR_VAL(tr->cls), old);
                    return -1;
                }
                object_init_ex(val, ce);
            }
        }
        storeRef(val, oht);
        if (tr->fmt & 1) { /* externalizable */
            ofs = decodeValue(newKey(val, "_data", sizeof("_data")), buf, pos, size, opts, sht, oht, tht);
            if (ofs < 0) return -1;
            pos += ofs;
        } else {
            int i;
            for (i = 0; i < tr->cnt; ++i) {
                ofs = decodeValue(newKey(val, tr->fld[i], tr->flen[i]), buf, pos, size, opts, sht, oht, tht);
                if (ofs < 0) return -1;
                pos += ofs;
            }
            if (tr->fmt & 2) { /* dynamic */
                for ( ;; ) {
                    ofs = decodeStr(&key, &klen, 0, buf, pos, size, 0, sht);
                    if (ofs < 0) return -1;
                    pos += ofs;
                    if (!klen) break;
                    if (map && !key[0]) {
                        php_error(E_WARNING, "Inappropriate class member name at position %d", pos);
                        return -1;
                    }
                    ofs = decodeValue(newKey(val, key, klen), buf, pos, size, opts, sht, oht, tht);
                    if (ofs < 0) return -1;
                    pos += ofs;
                }
            }
        }
        HT_ALLOW_COW_VIOLATION(HASH_OF(val));
        if (!map && tr->cls) add_assoc_stringl(val, "_class", ZSTR_VAL(tr->cls), ZSTR_LEN(tr->cls));
        else if (ce && (opts & AMF3_CLASS_CONSTRUCT)) { /* call the constructor */
            zend_call_method_with_0_params(val, ce, &ce->constructor, NULL, NULL);
            if (EG(exception)) return -1;
        }
    }
    return pos - old;
}

static int decodeValue(zval *val, const char *buf, int pos, int size, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
    int old = pos, ofs;
    if (pos >= size) {
        php_error(E_WARNING, "Insufficient type data at position %d", pos);
        return -1;
    }
    switch (buf[pos++]) {
        case AMF3_UNDEFINED:
        case AMF3_NULL:
            ZVAL_NULL(val);
            break;
        case AMF3_FALSE:
            ZVAL_FALSE(val);
            break;
        case AMF3_TRUE:
            ZVAL_TRUE(val);
            break;
        case AMF3_INTEGER:
            {
                int i;
                ofs = decodeU29(&i, buf, pos, size);
                if (ofs < 0) return -1;
                pos += ofs;
                if (i & 0x10000000) i -= 0x20000000;
                ZVAL_LONG(val, i);
                break;
            }
        case AMF3_DOUBLE:
            ofs = decodeDouble(val, buf, pos, size);
            if (ofs < 0) return -1;
            pos += ofs;
            break;
        case AMF3_STRING:
            ofs = decodeStr(0, 0, val, buf, pos, size, 0, sht);
            if (ofs < 0) return -1;
            pos += ofs;
            break;
        case AMF3_XML:
        case AMF3_XMLDOC:
        case AMF3_BYTEARRAY:
            ofs = decodeStr(0, 0, val, buf, pos, size, 1, oht);
            if (ofs < 0) return -1;
            pos += ofs;
            break;
        case AMF3_DATE:
            ofs = decodeDate(val, buf, pos, size, oht);
            if (ofs < 0) return -1;
            pos += ofs;
            break;
        case AMF3_ARRAY:
            ofs = decodeArray(val, buf, pos, size, opts, sht, oht, tht);
            if (ofs < 0) return -1;
            pos += ofs;
            break;
        case AMF3_OBJECT:
            ofs = decodeObject(val, buf, pos, size, opts, sht, oht, tht);
            if (ofs < 0) return -1;
            pos += ofs;
            break;
        default:
            php_error(E_WARNING, "Unsupported value of type %d at position %d", buf[pos - 1], pos);
            return -1;
    }
    return pos - old;
}

static void freeTraits(zval *val) {
    Traits *tr = Z_PTR_P(val);
    if (tr->cls) zend_string_release(tr->cls);
    efree(tr->fld);
    efree(tr->flen);
    efree(tr);
}

PHP_FUNCTION(amf3_decode) {
    const char *buf;
    size_t size;
    zval *count = 0;
    long opts = 0;
    HashTable sht, oht, tht;
    int ofs;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|z/l", &buf, &size, &count, &opts) == FAILURE) return;
    zend_hash_init(&sht, 0, 0, ZVAL_PTR_DTOR, 0);
    zend_hash_init(&oht, 0, 0, ZVAL_PTR_DTOR, 0);
    zend_hash_init(&tht, 0, 0, freeTraits, 0);
    ofs = decodeValue(return_value, buf, 0, size, opts, &sht, &oht, &tht);
    zend_hash_destroy(&sht);
    zend_hash_destroy(&oht);
    zend_hash_destroy(&tht);
    if (count) {
        zval_ptr_dtor(count);
        ZVAL_LONG(count, ofs);
    }
    if (ofs < 0) {
        zval_ptr_dtor(return_value);
        ZVAL_NULL(return_value);
    }
}
