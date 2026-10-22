--TEST--
AMF3 class mapping writes real properties and honors PHP property rules
--SKIPIF--
<?php
if (!extension_loaded("amf3")) die("skip: amf3 extension not available");
?>
--FILE--
<?php
function check($condition) {
    if (!$condition) throw new Exception('Check failed');
}
function packet($class, $value = "\x04\x2a", $name = 'x') {
    return "\x0a\x13" . chr(strlen($class) * 2 + 1) . $class
        . chr(strlen($name) * 2 + 1) . $name . $value;
}

class Box { public int $x = 0; }
$value = amf3_decode(packet(Box::class), $count, AMF3_CLASS_MAP);
check($value->x === 42 && (array)$value === ['x' => 42]);
check($count === strlen(packet(Box::class)));
try {
    amf3_decode(packet(Box::class, "\x06\x03a"), $count, AMF3_CLASS_MAP);
    throw new Exception('Expected TypeError');
} catch (TypeError $e) {}
class PrivateBox { private $x = 0; }
class ProtectedBox { protected $x = 0; }
readonly class ReadonlyBox { public int $x; }
class HiddenSetter { public private(set) int $x = 0; }
foreach ([PrivateBox::class, ProtectedBox::class, ReadonlyBox::class, HiddenSetter::class] as $class) {
    try {
        amf3_decode(packet($class), $count, AMF3_CLASS_MAP);
        throw new Exception('Expected property access error');
    } catch (Error $e) {}
}
class HookBox {
    public int $x = 0 { set { $this->x = $value * 2; } }
}
$value = amf3_decode(packet(HookBox::class), $count, AMF3_CLASS_MAP);
check($value->x === 84);
class ThrowingHook {
    public int $x { set { throw new RuntimeException('setter'); } }
}
try {
    amf3_decode(packet(ThrowingHook::class), $count, AMF3_CLASS_MAP);
    throw new Exception('Expected setter exception');
} catch (RuntimeException $e) { check($e->getMessage() === 'setter'); }
class CallerScope {
    private $x = 0;
    public static function decode() {
        try {
            amf3_decode(packet(self::class), $count, AMF3_CLASS_MAP);
            throw new Exception('Caller scope granted private access');
        } catch (Error $e) {}
    }
}
CallerScope::decode();
$value = amf3_decode("\x0a\x0b\x01\x03" . '1' . "\x04\x2a\x01");
check($value->{'1'} === 42 && array_keys((array)$value) === [1]);
// An externalizable value uses the literal property name, without a trailing NUL.
$value = amf3_decode("\x0a\x07\x01\x04\x2a");
check($value->_data === 42 && array_keys((array)$value) === ['_data']);
echo "OK\n";

?>
--EXPECT--
OK
