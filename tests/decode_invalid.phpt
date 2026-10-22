--TEST--
AMF3 malformed input and array index overflow report failure without crashing
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
$overflow = "\x09\x03\x27" . '9223372036854775807' . "\x04\x01\x01\x04\x02";
foreach (['', "\x09", "\x0a", "\x06\x00", "\x05\x00", $overflow] as $bytes) {
    $count = 123;
    check(amf3_decode($bytes, $count) === null && $count === -1);
}
restore_error_handler();
check(count($warnings) === 6);
check(str_contains($warnings[5], 'Cannot append array element'));
// Exceptions raised by a warning handler must survive cleanup.
set_error_handler(function () { throw new RuntimeException('warning handler'); });
try {
    amf3_decode("\x09", $count);
    throw new Exception('Expected warning handler exception');
} catch (RuntimeException $e) { check($e->getMessage() === 'warning handler'); }
restore_error_handler();
check(amf3_decode(amf3_encode([1, 'ok'])) === [1, 'ok']);
echo "OK\n";

?>
--EXPECT--
OK
