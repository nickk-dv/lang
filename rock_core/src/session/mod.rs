use crate::error::ErrorComp;
use crate::package;
use crate::text::{self, TextRange};
use std::path::PathBuf;

pub struct Session {
    cwd: PathBuf,
    files: Vec<File>,
    package: package::PackageData,
}

pub enum BuildKind {
    Debug,
    Release,
}

impl BuildKind {
    pub fn as_str(self) -> &'static str {
        match self {
            BuildKind::Debug => "debug",
            BuildKind::Release => "release",
        }
    }
}

pub struct File {
    pub path: PathBuf,
    pub source: String,
    pub line_ranges: Vec<TextRange>,
}

#[derive(Copy, Clone)]
pub struct FileID(u32);

impl Session {
    pub fn new() -> Result<Session, Vec<ErrorComp>> {
        create_session().map_err(|error| vec![error])
    }

    pub fn cwd(&self) -> &PathBuf {
        &self.cwd
    }
    pub fn file(&self, id: FileID) -> &File {
        &self.files[id.index()]
    }
    pub fn file_ids(&self) -> impl Iterator<Item = FileID> {
        (0..self.files.len()).map(FileID::new)
    }
    pub fn package(&self) -> &package::PackageData {
        &self.package
    }
}

impl FileID {
    fn new(index: usize) -> FileID {
        FileID(index as u32)
    }
    fn index(self) -> usize {
        self.0 as usize
    }
}

//@general display paths as relative to src folder?
// both in errors here, and diagnostic cli formats
// e.g: src/main.rock or ./src/main.rock
fn create_session() -> Result<Session, ErrorComp> {
    let cwd = std::env::current_dir().map_err(|io_error| {
        ErrorComp::message(format!(
            "failed to get current working directory, reason: {}",
            io_error
        ))
    })?;

    let src_dir = cwd.join("src");

    let name = cwd
        .file_name()
        .ok_or_else(|| ErrorComp::message("failed to get current working directory name"))?
        .to_str()
        .ok_or_else(|| ErrorComp::message("current working directory name is not valid utf-8"))?
        .to_string();

    //@temporary values for the manifest package data @16.04.24
    let kind = if src_dir.join("main.rock").exists() {
        package::PackageKind::Bin
    } else {
        package::PackageKind::Lib
    };
    let package =
        package::PackageData::new(name, None, kind, package::Semver::new(0, 1, 0), Vec::new());

    let read_dir = std::fs::read_dir(&src_dir).map_err(|io_error| {
        ErrorComp::message(format!(
            "failed to read directory: `{}`, reason: {}",
            src_dir.to_string_lossy(),
            io_error
        ))
    })?;

    let mut files = Vec::new();

    for entry in read_dir.flatten() {
        let path = entry.path();

        if path.is_file() && path.extension().unwrap_or_default() == "rock" {
            let source = std::fs::read_to_string(&path).map_err(|io_error| {
                ErrorComp::message(format!(
                    "failed to read file: `{}`, reason: {}",
                    path.to_string_lossy(),
                    io_error
                ))
            })?;
            let line_ranges = text::find_line_ranges(&source);
            files.push(File {
                path,
                source,
                line_ranges,
            });
        } else if path.is_dir() {
            //@communicate that directories in src folder of rock package are not allowed?
            // this can remove confusion about how module and package system is organized
            //@currently nested directories are ignored, and wont be parsed
            // lsp could produce a error about disconnected or invalid file in similar manner
        }
    }

    Ok(Session {
        cwd,
        files,
        package,
    })
}
