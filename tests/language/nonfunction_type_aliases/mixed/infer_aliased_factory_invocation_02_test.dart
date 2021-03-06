// Copyright (c) 2020, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
// @dart = 2.9
// Requirements=nnbd-weak

// SharedOptions=--enable-experiment=nonfunction-type-aliases

import 'package:expect/expect.dart';
import 'infer_aliased_factory_invocation_02_lib.dart';

void main() {
  T<num> x1 = T();
  C x2 = T();
}
