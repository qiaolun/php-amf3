--TEST--
AMF3 obj test 3
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

$t2 = new test1();
$t2->a = 'a2';
$t2->b = 'bb2';
$t2->c = 'ccc2';

$t3 = $t;


$bin = amf3_encode([$t, $t2, $t3]);
echo bin2hex($bin).PHP_EOL;

--EXPECT--
0907010a330b746573743103610362036306020605626206076363630a010605613206076262320609636363320a02
