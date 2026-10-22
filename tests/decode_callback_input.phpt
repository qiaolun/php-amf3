--TEST--
AMF3 retains its input while property setters mutate the source variable
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

class MutatingBox {
    public int $x { set { $GLOBALS['bytes'] = str_repeat('z', 65536); } }
}
// A second object reuses the first object's class and property name strings.
$object = packet(MutatingBox::class);
$bytes = "\x09\x05\x01" . $object . "\x0a\x01\x04\x2b";
$expectedSize = strlen($bytes);
$value = amf3_decode($bytes, $count, AMF3_CLASS_MAP);
check(count($value) === 2 && $value[0] instanceof MutatingBox && $value[1] instanceof MutatingBox);
check($count === $expectedSize && strlen($bytes) === 65536);
echo "OK\n";

?>
--EXPECT--
OK
