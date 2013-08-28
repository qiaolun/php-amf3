--TEST--
AMF3 array test 1, numeric keys only
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$arr = array(
    1 => 'aaa',
    2 => 'bbb',
    9 => 'ccc',
    19 => 'ddd',
);

$bin = amf3_encode($arr);

$out = amf3_decode($bin);

var_dump($out);

--EXPECT--
array(4) {
  [0]=>
  string(3) "aaa"
  [1]=>
  string(3) "bbb"
  [2]=>
  string(3) "ccc"
  [3]=>
  string(3) "ddd"
}
