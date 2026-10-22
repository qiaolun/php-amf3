--TEST--
AMF3 traits distinguish each ordered initialized property set
--SKIPIF--
<?php
if (!extension_loaded("amf3")) die("skip: amf3 extension not available");
?>
--FILE--
<?php
function check($condition) {
    if (!$condition) throw new Exception('Check failed');
}

#[AllowDynamicProperties]
class Shape {
    public int $x;
    public $_skip = 'secret';
    protected $hidden = 'secret';
}
$a = new Shape;
$a->x = 1;
$a->first = 'a';
$b = new Shape;
$b->second = 'b';
$c = new Shape;
$c->x = 2;
$c->first = 'c';
$d = new Shape;
$d->first = 'd';
$d->x = 3;
$bytes = amf3_encode([$a, $b, $c, $d]);
$value = amf3_decode($bytes, $count);
check($count === strlen($bytes));
check($value === [
    ['x' => 1, 'first' => 'a', '_class' => 'Shape'],
    ['second' => 'b', '_class' => 'Shape'],
    ['x' => 2, 'first' => 'c', '_class' => 'Shape'],
    ['x' => 3, 'first' => 'd', '_class' => 'Shape'],
]);
$value = amf3_decode($bytes, $count, AMF3_CLASS_MAP);
check($value[0]->x === 1 && !isset($value[1]->x) && $value[2]->x === 2);
check($value[0]->first === 'a' && $value[1]->second === 'b');
echo "OK\n";

?>
--EXPECT--
OK
