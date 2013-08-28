--TEST--
AMF3 array test 4, assoc array
--SKIPIF--
<?php
	if (!extension_loaded('amf3')) die('skip: amf3 extension not available');
?>
--FILE--
<?php

$arr = array(
    'a' => 'aaa',
    'b' => 'bbb',
    'c' => 'ccc',
    'd' => 'ddd',
    'e' => 'eee',
    'f' => 'fff',
);

$bin = amf3_encode($arr);

$out = amf3_decode($bin);

var_dump($out);

--EXPECT--
array(6) {
  ["a"]=>
  string(3) "aaa"
  ["b"]=>
  string(3) "bbb"
  ["c"]=>
  string(3) "ccc"
  ["d"]=>
  string(3) "ddd"
  ["e"]=>
  string(3) "eee"
  ["f"]=>
  string(3) "fff"
}
