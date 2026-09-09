"""Real temporary Git histories; no network or hardware."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from brrs_suite_case import git_provenance


class GitProvenanceTests(unittest.TestCase):
    def test_non_repository_is_explicitly_unknown(self):
        with tempfile.TemporaryDirectory() as td:
            meta,patch=git_provenance(Path(td))
            self.assertFalse(meta['available']);self.assertEqual(patch,b'')

    def test_clean_commit_and_dirty_changes_are_distinguished(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            def git(*args):
                return subprocess.check_output(['git','-C',str(root),*args],stderr=subprocess.STDOUT)
            git('init','-q');(root/'source.c').write_text('original\n');git('add','source.c')
            git('-c','user.name=Offline Test','-c','user.email=test@example.invalid',
                '-c','commit.gpgsign=false','commit','-qm','test fixture')
            commit=git('rev-parse','HEAD').decode().strip()
            meta,patch=git_provenance(root)
            self.assertEqual(meta['commit'],commit);self.assertFalse(meta['dirty'])
            self.assertTrue(meta['commit_alone_describes_worktree']);self.assertEqual(patch,b'')
            (root/'source.c').write_text('changed\n');(root/'untracked.c').write_text('extra\n')
            meta,patch=git_provenance(root)
            self.assertEqual(meta['commit'],commit);self.assertTrue(meta['dirty'])
            self.assertFalse(meta['commit_alone_describes_worktree'])
            self.assertIn('untracked.c',meta['status']);self.assertIn(b'+changed',patch)
            self.assertEqual(meta['tracked_diff_sha256'],hashlib.sha256(patch).hexdigest())


if __name__=='__main__':unittest.main(verbosity=2)
