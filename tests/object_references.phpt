--TEST--
AMF3 preserves distinct objects, shared objects and object cycles
--SKIPIF--
<?php
if (!extension_loaded("amf3")) die("skip: amf3 extension not available");
?>
--FILE--
<?php
function check($condition) {
    if (!$condition) throw new Exception('Check failed');
}

$a = (object)['x' => 1];
$b = (object)['x' => 2];
$value = amf3_decode(amf3_encode([$a, [], $b, $a, $b]), $count);
check($value[0] !== $value[2] && $value[0]->x === 1 && $value[2]->x === 2);
check($value[0] === $value[3] && $value[2] === $value[4]);
$a->self = $a;
$a->other = $b;
$b->other = $a;
$bytes = amf3_encode($a);
$value = amf3_decode($bytes, $count);
check($count === strlen($bytes));
check($value === $value->self && $value === $value->other->other);
echo "OK\n";

?>
--EXPECT--
OK
