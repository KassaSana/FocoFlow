import unittest


class ExportOnnxTests(unittest.TestCase):
    def test_module_imports(self) -> None:
        from ml import export_onnx

        self.assertTrue(callable(export_onnx.main))


if __name__ == "__main__":
    unittest.main()
