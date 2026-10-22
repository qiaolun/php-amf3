--TEST--
AMF3 count writes respect typed references and aliased input
--SKIPIF--
<?php
if (!extension_loaded("amf3")) die("skip: amf3 extension not available");
?>
--FILE--
<?php
function check($condition) {
    if (!$condition) throw new Exception('Check failed');
}

class Counts {
    public int $number = 0;
    public string $text = '';
    public array $items = [];
}
$c = new Counts;
check(amf3_decode("\x01", $c->number) === null && $c->number === 1);
check(amf3_decode("\x01", $c->text) === null && $c->text === '1');
try {
    amf3_decode("\x01", $c->items);
    throw new Exception('Expected typed reference error');
} catch (TypeError $e) { check($c->items === []); }
$bytes = "\x06\x03a";
check(amf3_decode($bytes, $bytes) === 'a' && $bytes === 3);
$alias =& $c->number;
check(amf3_decode("\x01", $alias) === null && $alias === 1 && $c->number === 1);
// Callback invocation can supply a temporary rather than a reference.
set_error_handler(function () { return true; });
check(call_user_func_array('amf3_decode', ["\x01", 0]) === null);
restore_error_handler();
echo "OK\n";

?>
--EXPECT--
OK
