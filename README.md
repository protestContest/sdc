# Surface Defect Correction

This is a reimplementation of the surface defect correction (a.k.a. "Digital ICE") that shipped with film scanners in the late 90s.

This implementation aims to reproduce the specific algorithm developed by Advanced Science Fiction and distributed with Nikon Scan 2.5 for Macintosh around 1998. The original binary library was disassembled and analyzed with the help of an LLM, and the algorithm reconstructed based on that implementation, with some minor differences:

- A bug was discovered in the original library in the inpainting step, where the source pixels for an inpainted pixel were calculated incorrectly. This implementation uses the most likely intended source pixel patterns.
- The specific input and output formats of this implementation differ - this requires a 4-channel TIFF file (R, G, B, IR), only supports 8-, 12-, and 16-bit channel depths.
- The original library supports lower-quality settings like fewer offset DCTs and a single-pass mode. This version only implements the highest-quality path.

## Example

The library works on negative color images. These examples have been inverted and adjusted from the test frame for demonstration.

Original dirty image:

![Original dirty image](https://raw.githubusercontent.com/protestContest/sdc/refs/heads/main/support/Example.dirty.tiff)

Cleaned image:

![Cleaned image](https://raw.githubusercontent.com/protestContest/sdc/refs/heads/main/support/Example.clean.tiff)
