"""Deterministic mobile app metadata/icon contracts; no SDK or third-party Python modules."""
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib
import mobile_assets as assets


def png_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def indexed_png(size, depth, pixels, palette, alpha=None, filters=None):
    rows = []
    stride = (size * depth + 7) // 8
    previous = bytes(stride)
    for y in range(size):
        row = bytearray(stride)
        for x in range(size):
            row[x * depth // 8] |= pixels[y * size + x] << (8 - depth - x * depth % 8)
        mode = filters[y] if filters else 0
        encoded = bytearray(stride)
        for x, value in enumerate(row):
            left, above, corner = row[x-1] if x else 0, previous[x], previous[x-1] if x else 0
            if mode == 0: predictor = 0
            elif mode == 1: predictor = left
            elif mode == 2: predictor = above
            elif mode == 3: predictor = (left + above) // 2
            else:
                guess = left + above - corner
                predictor = min((left, above, corner), key=lambda candidate: abs(candidate-guess))
            encoded[x] = (value - predictor) % 256
        rows.append(bytes([mode]) + encoded)
        previous = row
    header = png_chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, depth, 3, 0, 0, 0))
    return (assets.PNG + header + (png_chunk(b'PLTE', palette) if palette is not None else b'') +
            (png_chunk(b'tRNS', alpha) if alpha is not None else b'') +
            png_chunk(b'IDAT', zlib.compress(b''.join(rows))) + png_chunk(b'IEND', b''))


class MobileAssetsTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[1] / '.cache/mobile-assets-tests'
        root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=root)
        self.root = Path(self.temporary.name)

    def tearDown(self): self.temporary.cleanup()

    def icon(self, size=1024):
        path = self.root / 'icon.png'
        path.write_bytes(assets.png_bytes(size, bytes([30, 100, 210]) * size * size))
        return path

    def test_metadata_precedence_versions_and_missing_icon(self):
        project = {'title': 'Game', 'application': {'id': 'org.example.game', 'version': '2.4', 'build': 12,
                   'orientation': 'portrait', 'platforms': {'ios': {'id': 'org.example.ios', 'orientation': 'sensor'}}}}
        result = assets.metadata(project, 'ios', overrides={'orientation': 'landscape'})
        self.assertEqual((result['id'], result['version'], result['build'], result['orientation']),
                         ('org.example.ios', '2.4', 12, 'landscape'))
        self.assertEqual(len(result['warnings']), 1)
        with self.assertRaisesRegex(ValueError, 'requires an icon'): assets.metadata(project, 'ios', 'release')
        legacy = assets.metadata({'title': 'Old game'}, 'ios', 'release', {'id': 'org.example.legacy'})
        self.assertEqual(legacy['warnings'], []); self.assertEqual(legacy['version'], assets.engine_version())

    def test_metadata_bounds(self):
        for patch in [{'build': True}, {'build': 0}, {'build': 2100000001}, {'version': 'v1'}, {'version': '1.0\n'},
                      {'id': 'one'}, {'label': 'bad\nname'}, {'orientation': 'upside-down'}]:
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                assets.metadata({'application': dict(id='org.example.game', **{k: v for k,v in patch.items() if k != 'id'})}, 'ios', overrides=patch)

    def test_png_integrity_paths_and_suitability(self):
        path = self.icon(192)
        self.assertEqual(assets.decode_png(path)[0], 192)
        with self.assertRaisesRegex(ValueError, '1024'): assets.decode_png(path, 1024)
        data = bytearray(path.read_bytes()); data[-8] ^= 1; path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, 'checksum'): assets.decode_png(path)
        for name in ['../icon.png', '/icon.png', 'a\\icon.png', './icon.png', 'a//icon.png']:
            with self.subTest(name=name), self.assertRaises(ValueError): assets.icon_path(self.root, name)

    def test_png_rejects_alpha_and_interlacing(self):
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data) & 0xffffffff)
        path = self.root / 'rgba.png'
        for alpha, interlace, error in [(0, 0, 'opaque'), (255, 1, 'non-interlaced')]:
            header = struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, interlace)
            path.write_bytes(assets.PNG + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(bytes([0, 30, 100, 210, alpha]))) + chunk(b'IEND', b''))
            with self.assertRaisesRegex(ValueError, error): assets.decode_png(path, 1)

    def test_indexed_png_depths_filters_and_partial_byte_rows(self):
        palette = bytes([17, 80, 240, 255, 214, 85])
        pixels = [(x + y * 3) % 2 for y in range(5) for x in range(5)]
        expected = b''.join(palette[index*3:index*3+3] for index in pixels)
        for depth in (1, 2, 4, 8):
            with self.subTest(depth=depth):
                path = self.root / ('indexed-%s.png' % depth)
                path.write_bytes(indexed_png(5, depth, pixels, palette, b'\xff\xff', [0, 1, 2, 3, 4]))
                self.assertEqual(assets.decode_png(path, 1), (5, expected))

    def test_indexed_png_rejects_invalid_palette_and_used_transparency(self):
        palette = bytes([17, 80, 240, 255, 214, 85])
        path = self.root / 'bad-indexed.png'
        for data, error in [
            (indexed_png(1, 1, [0], None), 'palette'),
            (indexed_png(1, 1, [0], b'12'), 'palette'),
            (indexed_png(1, 1, [0], palette + b'123'), 'palette'),
            (indexed_png(1, 8, [0], b'123' * 257), 'palette'),
            (indexed_png(1, 2, [2], palette), 'missing palette'),
            (indexed_png(1, 1, [0], palette, b'\xff' * 3), 'transparency'),
            (indexed_png(1, 1, [1], palette, b'\xff\x00'), 'opaque'),
            (indexed_png(1, 1, [1], palette, b'\xff\x80'), 'opaque'),
        ]:
            with self.subTest(error=error):
                path.write_bytes(data)
                with self.assertRaisesRegex(ValueError, error): assets.decode_png(path, 1)
        # An unused transparent swatch does not make the actual artwork translucent.
        path.write_bytes(indexed_png(1, 1, [0], palette, b'\xff\x00'))
        self.assertEqual(assets.decode_png(path, 1), (1, palette[:3]))
        path.write_bytes(indexed_png(1, 1, [1], palette, b'\xff'))
        self.assertEqual(assets.decode_png(path, 1), (1, palette[3:]))

    def test_native_sprite_export_is_accepted_without_external_conversion(self):
        root = Path(__file__).resolve().parents[1]
        project = root / 'examples/workflow-2d'
        icon = project / 'assets/icon.png'
        expected_size, expected_rgb = assets.decode_png(icon, 1024)
        self.assertEqual(expected_size, 1024)
        recipe = json.loads((project / 'assets/icon.sprite.json').read_text())
        colors = {key: bytes.fromhex(value.lstrip('#')) for key, value in recipe['palette'].items()}
        expected = b''.join(b''.join(colors[pixel] * 8 for pixel in row) * 8 for row in recipe['frames'][0]['pixels'])
        self.assertEqual(expected_rgb, expected)
        engine = next((candidate for candidate in [root / 'build/langmake.exe', root / 'langmake.exe'] if candidate.is_file()), None)
        if engine is not None:
            exported = self.root / 'native-export.png'
            result = subprocess.run([str(engine), 'asset', 'render', '--project', str(project), '--id', 'app_icon',
                                     '--output', str(exported), '--scale', '8'], capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout.decode('utf-8', 'replace'))
            self.assertEqual(assets.decode_png(exported, 1024), (expected_size, expected_rgb))

    def test_both_platforms_generate_real_icons_and_hashes(self):
        source = self.icon()
        for platform in ['ios', 'android']:
            output = self.root / platform
            report = assets.generate_icon(source, output, platform)
            self.assertEqual(report['source_sha256'], assets.sha256(source))
            self.assertGreater(len(report['files']), 2)
            for item in report['files']:
                self.assertEqual(item['sha256'], assets.sha256(output / item['path']))
                if item['path'].endswith('.png'):
                    size, rgb = assets.decode_png(output / item['path'], 1)
                    self.assertEqual(rgb, bytes([30,100,210]) * size * size)
            if platform == 'ios':
                catalog = json.loads((output / 'Assets.xcassets/AppIcon.appiconset/Contents.json').read_text())
                self.assertEqual(catalog['images'][0]['size'], '1024x1024')
            else:
                self.assertIn('adaptive-icon', (output / 'mipmap-anydpi-v26/ic_launcher.xml').read_text())


if __name__ == '__main__': unittest.main(verbosity=2)
