--TEST--
AMF3 detects recursive arrays and enforces a 512-container depth limit
--SKIPIF--
<?php
if (!extension_loaded("amf3")) die("skip: amf3 extension not available");
?>
--FILE--
<?php
function check($condition) {
    if (!$condition) throw new Exception('Check failed');
}

$warnings = [];
set_error_handler(function ($type, $message) use (&$warnings) { $warnings[] = $message; return true; });
$array = [];
$array['self'] =& $array;
check(amf3_encode($array) === false);
check(amf3_encode($array, AMF3_FORCE_OBJECT) === false);
check(count($warnings) === 2 && str_contains($warnings[0], 'Recursive arrays'));
$shared = [1];
check(amf3_decode(amf3_encode([$shared, $shared])) === [[1], [1]]);
$nested = null;
for ($i = 0; $i < 512; ++$i) $nested = [$nested];
$bytes = amf3_encode($nested);
check(is_string($bytes));
check(amf3_decode($bytes, $count) === $nested && $count === strlen($bytes));
check(amf3_encode([$nested]) === false);
check(amf3_decode("\x09\x03\x01" . $bytes, $count) === null && $count === -1);
$object = null;
for ($i = 0; $i < 512; ++$i) $object = (object)['child' => $object];
$bytes = amf3_encode($object);
check(is_string($bytes));
$decoded = amf3_decode($bytes, $count);
check($count === strlen($bytes));
for ($i = 0; $i < 512; ++$i) $decoded = $decoded->child;
check($decoded === null);
check(amf3_encode((object)['child' => $object]) === false);
check(amf3_decode("\x09\x03\x01" . $bytes, $count) === null && $count === -1);
check(count($warnings) === 6);
restore_error_handler();
set_error_handler(function () { throw new RuntimeException('encoding warning'); });
try {
    amf3_encode($array);
    throw new Exception('Expected encoding warning exception');
} catch (RuntimeException $e) { check($e->getMessage() === 'encoding warning'); }
restore_error_handler();
check(amf3_decode(amf3_encode(['ok'])) === ['ok']);
echo "OK\n";

?>
--EXPECT--
OK
