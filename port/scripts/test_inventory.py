import pathlib, tempfile, unittest
import inventory

SAMPLE = (
    "void f() {\n"
    "  pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);\n"
    "  pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);\n"
    "  m_pd3dDevice->SetTexture(0, NULL);\n"
    "  D3DXMatrixIdentity(&m);\n"
    "  CString s; MessageBox(NULL, \"x\", \"y\", 0);\n"
    "  // \xb1\xe2\xba\xbb CP949 comment bytes\n"
    "}\n"
)

class InventoryTest(unittest.TestCase):
    def test_counts_calls_in_cp949_source(self):
        with tempfile.TemporaryDirectory() as d:
            mod = pathlib.Path(d, "[Lib]__Engine", "Sources")
            mod.mkdir(parents=True)
            (mod / "a.cpp").write_bytes(SAMPLE.encode("latin-1"))
            counts = inventory.scan(pathlib.Path(d), ["[Lib]__Engine"])
        self.assertEqual(counts["d3d9_method"]["SetRenderState"], 2)
        self.assertEqual(counts["d3d9_method"]["SetTexture"], 1)
        self.assertEqual(counts["d3dx_call"]["D3DXMatrixIdentity"], 1)
        self.assertEqual(counts["win32_call"]["MessageBox"], 1)
        self.assertEqual(counts["mfc_type"]["CString"], 1)

    def test_render_lists_each_category(self):
        import collections
        counts = {k: collections.Counter() for k in inventory.CATEGORIES}
        counts["d3d9_method"]["SetRenderState"] = 3
        md = inventory.render(counts)
        self.assertIn("## d3d9_method", md)
        self.assertIn("| SetRenderState | 3 |", md)

if __name__ == "__main__":
    unittest.main()
