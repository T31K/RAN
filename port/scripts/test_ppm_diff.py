import pathlib, tempfile, unittest
import ppm_diff

def write_ppm(path, w, h, rgb):
    path.write_bytes(b"P6\n%d %d\n255\n" % (w, h) + bytes(rgb) * (w * h))

class PpmDiffTest(unittest.TestCase):
    def test_identical_images_have_no_bad_pixels(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d, "a.ppm"), pathlib.Path(d, "b.ppm")
            write_ppm(a, 4, 4, (10, 20, 30)); write_ppm(b, 4, 4, (10, 20, 30))
            self.assertEqual(ppm_diff.compare(a, b, tol=8), (0, 16))

    def test_small_differences_within_tolerance_pass(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d, "a.ppm"), pathlib.Path(d, "b.ppm")
            write_ppm(a, 4, 4, (10, 20, 30)); write_ppm(b, 4, 4, (15, 20, 30))
            self.assertEqual(ppm_diff.compare(a, b, tol=8), (0, 16))

    def test_large_difference_counts_every_pixel(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d, "a.ppm"), pathlib.Path(d, "b.ppm")
            write_ppm(a, 4, 4, (0, 0, 0)); write_ppm(b, 4, 4, (0, 0, 255))
            self.assertEqual(ppm_diff.compare(a, b, tol=8), (16, 16))

    def test_size_mismatch_raises(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d, "a.ppm"), pathlib.Path(d, "b.ppm")
            write_ppm(a, 4, 4, (0, 0, 0)); write_ppm(b, 2, 2, (0, 0, 0))
            with self.assertRaises(ValueError):
                ppm_diff.compare(a, b, tol=8)

if __name__ == "__main__":
    unittest.main()
