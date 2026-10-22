--TEST--
AMF3 constructors and invalid mapped classes fail safely
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

class Constructed {
    public int $x = 0;
    public function __construct() { check($this->x === 42); $this->x++; }
}
$value = amf3_decode(packet(Constructed::class), $count, AMF3_CLASS_MAP | AMF3_CLASS_CONSTRUCT);
check($value->x === 43);
$value = amf3_decode(packet(Constructed::class), $count, AMF3_CLASS_MAP);
check($value->x === 42);
class NoConstructor { public $x; }
$value = amf3_decode(packet(NoConstructor::class), $count, AMF3_CLASS_MAP | AMF3_CLASS_CONSTRUCT);
check($value->x === 42);
class ThrowsConstructor {
    public $x;
    function __construct() { throw new RuntimeException('constructor'); }
}
try {
    amf3_decode(packet(ThrowsConstructor::class), $count, AMF3_CLASS_MAP | AMF3_CLASS_CONSTRUCT);
    throw new Exception('Expected constructor exception');
} catch (RuntimeException $e) { check($e->getMessage() === 'constructor'); }
class RequiredConstructor { public $x; function __construct($required) {} }
class PrivateConstructor { public $x; private function __construct() {} }
abstract class AbstractBox { public $x; }
interface InterfaceBox {}
foreach ([RequiredConstructor::class, PrivateConstructor::class, AbstractBox::class, InterfaceBox::class] as $class) {
    try {
        amf3_decode(packet($class), $count, AMF3_CLASS_MAP | AMF3_CLASS_CONSTRUCT);
        throw new Exception('Expected class/constructor error');
    } catch (Error $e) {}
}
spl_autoload_register(function ($class) { throw new RuntimeException('autoload'); });
try {
    amf3_decode(packet('MissingBox'), $count, AMF3_CLASS_MAP | AMF3_CLASS_AUTOLOAD);
    throw new Exception('Expected autoload exception');
} catch (RuntimeException $e) { check($e->getMessage() === 'autoload'); }
echo "OK\n";

?>
--EXPECT--
OK
