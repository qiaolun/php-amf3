--TEST--
AMF3 obj test 1
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

class test1 {
    public $a;
    public $b;
    public $c;
}

$t = new test1();
$t->a = 'a';
$t->b = 'bb';
$t->c = 'ccc';

$bin = amf3_encode($t);
echo bin2hex($bin).PHP_EOL;

--EXPECT--
0a330b74657374310361036203630602060562620607636363
