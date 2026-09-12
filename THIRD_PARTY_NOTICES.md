# Third-Party Notices

This library's own code is MIT-licensed — see [LICENSE](LICENSE). It also
reuses two files verbatim from Pierre Molinaro's ACAN family of libraries, and
its overall API design (a `Settings` object that computes explicit
BRP/PROP_SEG/PHASE_SEG1/PHASE_SEG2/SJW from a desired bit rate and sample
point) is deliberately modeled after his `ACANFD_STM32` / `ACAN2517FD`
libraries so that code ports between them easily.

## Reused files

- [`src/ACAN_ESP32FD_CANMessage.h`](src/ACAN_ESP32FD_CANMessage.h)
- [`src/ACAN_ESP32FD_CANFDMessage.h`](src/ACAN_ESP32FD_CANFDMessage.h)

Both carry a header comment noting they are shared, unmodified, across:

- https://github.com/pierremolinaro/acan
- https://github.com/pierremolinaro/acan2515
- https://github.com/pierremolinaro/acan2517
- https://github.com/pierremolinaro/acan2517FD
- https://github.com/pierremolinaro/acanfd-stm32

## License of the reused files

Pierre Molinaro's ACAN libraries are released under the MIT License:

```
MIT License

Copyright (c) Pierre Molinaro

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

(Reproduced from the LICENSE files of
[acan2517FD](https://github.com/pierremolinaro/acan2517FD/blob/master/LICENSE)
and
[acanfd-stm32](https://github.com/pierremolinaro/acanfd-stm32/blob/master/LICENSE);
both are copyright Pierre Molinaro and MIT-licensed.)
