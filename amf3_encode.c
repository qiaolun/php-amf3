/*
 ** Copyright (C) 2010, 2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
 ** Please read the LICENSE file for license details
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_amf3.h"
#include "zend_smart_str.h"
#include "ext/spl/php_spl.h"
#include "amf3.h"


static void encodeValue(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht);

static void encodeU29(smart_str *ss, int val) {
    char buf[4];
    int size;
    val &= 0x1fffffff;
    if (val <= 0x7f) {
        buf[0] = val;
        size = 1;
    } else if (val <= 0x3fff) {
        buf[1] = val & 0x7f;
        val >>= 7;
        buf[0] = val | 0x80;
        size = 2;
    } else if (val <= 0x1fffff) {
        buf[2] = val & 0x7f;
        val >>= 7;
        buf[1] = val | 0x80;
        val >>= 7;
        buf[0] = val | 0x80;
        size = 3;
    } else {
        buf[3] = val;
        val >>= 8;
        buf[2] = val | 0x80;
        val >>= 7;
        buf[1] = val | 0x80;
        val >>= 7;
        buf[0] = val | 0x80;
        size = 4;
    }
    smart_str_appendl(ss, buf, size);
}

static void encodeDouble(smart_str *ss, double val) {
    union { int i; char c; } t;
    union { double d; char c[8]; } u;
    char buf[8];
    t.i = 1;
    u.d = val;
    if (!t.c) memcpy(buf, u.c, 8);
    else { /* little-endian machine */
        int i;
        for (i = 0; i < 8; ++i) buf[7 - i] = u.c[i];
    }
    smart_str_appendl(ss, buf, 8);
}

static int encodeRefStr(smart_str *ss, const char *str, size_t len, HashTable *ht) {
    int *oidx, nidx;
    if ((oidx = zend_hash_str_find_ptr(ht, str, len))) {
        encodeU29(ss, *oidx << 1);
        return 1;
    }
    nidx = zend_hash_num_elements(ht);
    if (nidx <= AMF3_MAX_INT) zend_hash_str_add_mem(ht, str, len, &nidx, sizeof(nidx));
    return 0;
}

static int encodeRefObj(smart_str *ss, zval *val, HashTable *ht) {
    int *oidx, nidx;
	zend_string* objectHash;

    objectHash = php_spl_object_hash(val);
    if ((oidx = zend_hash_str_find_ptr(ht, ZSTR_VAL(objectHash), ZSTR_LEN(objectHash)))) {
        zend_string_release(objectHash);
        encodeU29(ss, *oidx << 1);
        return 1;
    }

    nidx = zend_hash_num_elements(ht);
    if (nidx <= AMF3_MAX_INT) zend_hash_str_add_mem(ht, ZSTR_VAL(objectHash), ZSTR_LEN(objectHash), &nidx, sizeof(nidx));
    zend_string_release(objectHash);
    return 0;
}

static void encodeStr(smart_str *ss, const char *str, size_t len, HashTable *ht) {
    if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
    if (len && encodeRefStr(ss, str, len, ht)) return; /* Empty string is never sent by reference */
    encodeU29(ss, (len << 1) | 1);
    smart_str_appendl(ss, str, len);
}

static void encodeHash(smart_str *ss, HashTable *ht, int opts, int prv, HashTable *sht, HashTable *oht, HashTable *tht) {
    zend_ulong idx;
    zend_string *key;
    zval *val;
    ZEND_HASH_FOREACH_KEY_VAL(ht, idx, key, val) {
        if (key) {
            const char *str = ZSTR_VAL(key);
            size_t len = ZSTR_LEN(key);
            if (!len) continue; /* Empty key can't be represented in AMF3 */
            if (prv && !str[0]) continue; /* Skip private/protected property */
            encodeStr(ss, str, len, sht);
        } else {
            char buf[22];
            encodeStr(ss, buf, sprintf(buf, "%ld", idx), sht);
        }
        encodeValue(ss, val, opts, sht, oht, tht);
    } ZEND_HASH_FOREACH_END();
    smart_str_appendc(ss, 0x01);
}

static int getHashNumericKeyLen(HashTable *ht) {
    zval *val;
    zend_string *str_index;
    zend_ulong num_index;
    int len = 0;

    ZEND_HASH_FOREACH_KEY_VAL(ht, num_index, str_index, val) {
        if (str_index) {
            continue;
        }

        ++len;
    } ZEND_HASH_FOREACH_END();

    return len;
}

static int getHashKeyLen(HashTable *ht) {
    zval *val;
    zend_string *str_index;
    zend_ulong num_index;
    int len = 0;
    char *key;

    ZEND_HASH_FOREACH_KEY_VAL(ht, num_index, str_index, val) {
        if (str_index) {
            if (ZSTR_LEN(str_index) <= 0) {
                continue; /* empty key can't be represented in AMF3 */
            }

            key = ZSTR_VAL(str_index);
            if (!key[0]) {
                continue; /* skip private/protected property */
            }

            if (key[0] == '_') {
                continue;
            }

            ++len;
        } else {
            ++len;
        }
    } ZEND_HASH_FOREACH_END();

    return len;
}

static void encodeArray(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
    HashTable *ht = HASH_OF(val);

    zval *hv;
    zend_string *str_index;
    zend_ulong num_index;
    int numeric_key_len;
    int nidx;

    nidx = zend_hash_num_elements(ht);
    if (nidx <= AMF3_MAX_INT) zend_hash_next_index_insert_mem(oht, &nidx, sizeof(nidx));

    numeric_key_len = getHashNumericKeyLen(ht);
    if (numeric_key_len > AMF3_MAX_INT) numeric_key_len = AMF3_MAX_INT;
    encodeU29(ss, (numeric_key_len << 1) | 1);

    ZEND_HASH_FOREACH_KEY_VAL(ht, num_index, str_index, hv) {
        if(str_index) {
            if (ZSTR_LEN(str_index) <= 0) {
                continue; /* empty key can't be represented in AMF3 */
            }

            encodeStr(ss, ZSTR_VAL(str_index), ZSTR_LEN(str_index), sht);
            encodeValue(ss, hv, opts, sht, oht, tht);
        }
    } ZEND_HASH_FOREACH_END();

    smart_str_appendc(ss, 0x01);

    ZEND_HASH_FOREACH_KEY_VAL(ht, num_index, str_index, hv) {
        if(str_index) {
            continue;
        }

        encodeValue(ss, hv, opts, sht, oht, tht);
    } ZEND_HASH_FOREACH_END();
}

static void encodeObject(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
    HashTable *ht;

    zval *hv;
    zend_string *str_index;
    zend_ulong num_index;

    int numeric_key_len;

    zend_class_entry *ce = Z_TYPE_P(val) == IS_OBJECT ? Z_OBJCE_P(val) : zend_standard_class_def;

    int *oidx, nidx;
    int writeTraits, encoding, traitsInfo, propCount;

    char *key, kbuf[22];

    if (encodeRefObj(ss, val, oht)) return;

    ht = HASH_OF(val);

    writeTraits = 1;

    propCount = 0;

    if(ce == zend_standard_class_def) {
        encoding = ET_DYNAMIC;
    } else {
        encoding = ET_PROPLIST;
        propCount = getHashKeyLen(ht);
    }

    if ((oidx = zend_hash_str_find_ptr(tht, (char *)&ce, sizeof(ce)))) {
        encodeU29(ss, (*oidx << 2) | 1);
        writeTraits = 0;
    } else {
        nidx = zend_hash_num_elements(tht);
        if (nidx <= AMF3_MAX_INT) {
            zend_hash_str_add_mem(tht, (char *)&ce, sizeof(ce), &nidx, sizeof(nidx));
        }

        traitsInfo = AMF3_OBJECT_ENCODING;
        traitsInfo |= encoding << 2;
        traitsInfo |= propCount << 4;

        encodeU29(ss, traitsInfo);
    }

    if(writeTraits) {

        if(ce == zend_standard_class_def) {
            smart_str_appendc(ss, 0x01);
        } else {
            encodeStr(ss, ZSTR_VAL(ce->name), ZSTR_LEN(ce->name), sht); /* typed object */

            ZEND_HASH_FOREACH_KEY_VAL(ht, num_index, str_index, hv) {
                if (str_index) {
                    if (ZSTR_LEN(str_index) <= 0) {
                        continue; /* empty key can't be represented in AMF3 */
                    }
                    key = ZSTR_VAL(str_index);
                    if (!key[0]) {
                        continue; /* skip private/protected property */
                    }
                    if (key[0] == '_') {
                        continue;
                    }
                    encodeStr(ss, ZSTR_VAL(str_index), ZSTR_LEN(str_index), sht);
                } else {
                    encodeStr(ss, kbuf, sprintf(kbuf, "%ld", num_index), sht);
                }
            } ZEND_HASH_FOREACH_END();

        }
    }

    switch(encoding) {
        case ET_PROPLIST:
            ZEND_HASH_FOREACH_KEY_VAL(ht, num_index, str_index, hv) {
                if (str_index) {
                    if (ZSTR_LEN(str_index) <= 0) {
                        continue; /* empty key can't be represented in AMF3 */
                    }
                    key = ZSTR_VAL(str_index);

                    if (!key[0]) {
                        continue; /* skip private/protected property */
                    }
                    if (key[0] == '_') {
                        continue;
                    }

                    encodeValue(ss, hv, opts, sht, oht, tht);
                } else {
                    encodeValue(ss, hv, opts, sht, oht, tht);
                }
            } ZEND_HASH_FOREACH_END();
            break;

        case ET_DYNAMIC:
            ZEND_HASH_FOREACH_KEY_VAL(ht, num_index, str_index, hv) {
                if (str_index) {
                    if (ZSTR_LEN(str_index) <= 0) {
                        continue; /* empty key can't be represented in AMF3 */
                    }
                    key = ZSTR_VAL(str_index);

                    if (!key[0]) {
                        continue; /* skip private/protected property */
                    }
                    if (key[0] == '_') {
                        continue;
                    }

                    encodeStr(ss, ZSTR_VAL(str_index), ZSTR_LEN(str_index), sht);
                } else {
                    encodeStr(ss, kbuf, sprintf(kbuf, "%ld", num_index), sht);
                }
                encodeValue(ss, hv, opts, sht, oht, tht);
            } ZEND_HASH_FOREACH_END();

            smart_str_appendc(ss, 0x01);
            break;
    }

}

static void encodeValue(smart_str *ss, zval *val, int opts, HashTable *sht, HashTable *oht, HashTable *tht) {
    switch (Z_TYPE_P(val)) {
        default:
            smart_str_appendc(ss, AMF3_UNDEFINED);
            break;
        case IS_NULL:
            smart_str_appendc(ss, AMF3_NULL);
            break;
        case IS_FALSE:
            smart_str_appendc(ss, AMF3_FALSE);
            break;
        case IS_TRUE:
            smart_str_appendc(ss, AMF3_TRUE);
            break;
        case IS_LONG:
            {
                zend_long i = Z_LVAL_P(val);
                if ((i >= AMF3_MIN_INT) && (i <= AMF3_MAX_INT)) {
                    smart_str_appendc(ss, AMF3_INTEGER);
                    encodeU29(ss, i);
                } else {
                    smart_str_appendc(ss, AMF3_DOUBLE);
                    encodeDouble(ss, i);
                }
            }
            break;
        case IS_DOUBLE:
            smart_str_appendc(ss, AMF3_DOUBLE);
            encodeDouble(ss, Z_DVAL_P(val));
            break;
        case IS_STRING:
            smart_str_appendc(ss, AMF3_STRING);
            encodeStr(ss, Z_STRVAL_P(val), Z_STRLEN_P(val), sht);
            break;
        case IS_ARRAY:
            if (!(opts & AMF3_FORCE_OBJECT)) {
                smart_str_appendc(ss, AMF3_ARRAY);
                encodeArray(ss, val, opts, sht, oht, tht);
                break;
            } /* fall through; encode array as object */
        case IS_OBJECT:
            smart_str_appendc(ss, AMF3_OBJECT);
            encodeObject(ss, val, opts, sht, oht, tht);
            break;
        case IS_REFERENCE:
            encodeValue(ss, Z_REFVAL_P(val), opts, sht, oht, tht);
            break;
    }
}

static void freePtr(zval *val) {
    efree(Z_PTR_P(val));
}

PHP_FUNCTION(amf3_encode) {
    smart_str ss = {0};
    zval *val;
    zend_long opts = 0;
    HashTable sht, oht, tht;
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "z|l", &val, &opts) == FAILURE) return;
    zend_hash_init(&sht, 0, 0, freePtr, 0);
    zend_hash_init(&oht, 0, 0, freePtr, 0);
    zend_hash_init(&tht, 0, 0, freePtr, 0);
    encodeValue(&ss, val, opts, &sht, &oht, &tht);
    zend_hash_destroy(&sht);
    zend_hash_destroy(&oht);
    zend_hash_destroy(&tht);
    smart_str_0(&ss);
    RETURN_STR(ss.s);
}
