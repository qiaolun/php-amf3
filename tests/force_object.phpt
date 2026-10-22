--TEST--
AMF3_FORCE_OBJECT safely encodes arrays and reserves object reference slots
--SKIPIF--
<?php
if (!extension_loaded("amf3")) die("skip: amf3 extension not available");
?>
--FILE--
<?php
function check($condition) {
    if (!$condition) throw new Exception('Check failed');
}

check(bin2hex(amf3_encode([], AMF3_FORCE_OBJECT)) === '0a0b0101');
$data = [2 => 'two', -3 => null, 'name' => 'ok', 'nested' => [5 => 7]];
$bytes = amf3_encode($data, AMF3_FORCE_OBJECT);
$value = amf3_decode($bytes, $count);
check($count === strlen($bytes) && $value instanceof stdClass);
check($value->{'2'} === 'two' && $value->{'-3'} === null && $value->name === 'ok');
check($value->nested instanceof stdClass && $value->nested->{'5'} === 7);
$object = (object)['x' => 42];
$array = ['value' => 7];
$value = amf3_decode(amf3_encode([$array, $object, $array, $object], AMF3_FORCE_OBJECT));
check($value->{'0'} !== $value->{'2'});
check($value->{'0'}->value === 7 && $value->{'2'}->value === 7);
check($value->{'1'} === $value->{'3'} && $value->{'1'}->x === 42);
echo "OK\n";

?>
--EXPECT--
OK
