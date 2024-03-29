#![forbid(unsafe_code)]

use lsp_server::{Connection, ExtractError, Message, Response};
use lsp_types::notification::{self, Notification};
use lsp_types::request::{self, Request};
use std::error::Error;

fn main() -> Result<(), Box<dyn Error + Sync + Send>> {
    let (connection, io_threads) = Connection::stdio();

    let server_capabilities = serde_json::to_value(&lsp_types::ServerCapabilities {
        text_document_sync: Some(lsp_types::TextDocumentSyncCapability::Kind(
            lsp_types::TextDocumentSyncKind::INCREMENTAL,
        )),
        completion_provider: Some(lsp_types::CompletionOptions {
            ..Default::default()
        }),
        ..Default::default()
    })
    .unwrap();

    let initialization_params = match connection.initialize(server_capabilities) {
        Ok(it) => it,
        Err(e) => {
            if e.channel_is_disconnected() {
                io_threads.join()?;
            }
            return Err(e.into());
        }
    };
    let params: lsp_types::InitializeParams =
        serde_json::from_value(initialization_params).unwrap();

    main_loop(connection)?;
    io_threads.join()?;
    Ok(())
}

fn main_loop(connection: Connection) -> Result<(), Box<dyn Error + Sync + Send>> {
    for msg in &connection.receiver {
        match msg {
            Message::Request(req) => {
                if connection.handle_shutdown(&req)? {
                    return Ok(());
                }
                eprintln!("\nGOT REQUEST: {req:?}\n");
                handle_request(&connection, req);
            }
            Message::Response(resp) => {
                eprintln!("\nGOT RESPONSE: {resp:?}\n");
                handle_responce(&connection, resp);
            }
            Message::Notification(not) => {
                eprintln!("\nGOT NOTIFICATION: {not:?}\n");
                handle_notification(&connection, not);
            }
        }
    }
    Ok(())
}

fn send<Content: Into<Message>>(conn: &Connection, msg: Content) {
    conn.sender.send(msg.into()).expect("send failed");
}

fn cast_req<P>(
    req: lsp_server::Request,
) -> Result<(lsp_server::RequestId, P::Params), ExtractError<lsp_server::Request>>
where
    P: lsp_types::request::Request,
    P::Params: serde::de::DeserializeOwned,
{
    req.extract(P::METHOD)
}

fn cast_not<P>(
    not: lsp_server::Notification,
) -> Result<P::Params, ExtractError<lsp_server::Notification>>
where
    P: notification::Notification,
    P::Params: serde::de::DeserializeOwned,
{
    not.extract(P::METHOD)
}

fn handle_request(conn: &Connection, req: lsp_server::Request) {
    match req.method.as_str() {
        request::Completion::METHOD => {
            let (id, params) = cast_req::<request::Completion>(req).unwrap();
        }
        _ => {}
    }
}

fn handle_responce(conn: &Connection, resp: lsp_server::Response) {}

fn handle_notification(conn: &Connection, not: lsp_server::Notification) {
    match not.method.as_str() {
        notification::Cancel::METHOD => {
            let params = cast_not::<notification::Cancel>(not).unwrap();
        }
        notification::DidChangeTextDocument::METHOD => {
            let params = cast_not::<notification::DidChangeTextDocument>(not).unwrap();
        }
        notification::DidSaveTextDocument::METHOD => {
            let params = cast_not::<notification::DidSaveTextDocument>(not).unwrap();

            let publish_diagnostics = run_diagnostics();
            for publish in publish_diagnostics.iter() {
                send(
                    conn,
                    lsp_server::Notification::new(
                        notification::PublishDiagnostics::METHOD.into(),
                        publish,
                    ),
                );
            }
        }
        _ => {}
    }
}

use rock_core::ast_parse;
use rock_core::error::{ErrorComp, ErrorSeverity};
use rock_core::hir_lower;
use rock_core::session::Session;
use rock_core::text;

use lsp_types::{
    Diagnostic, DiagnosticRelatedInformation, DiagnosticSeverity, Location, Position,
    PublishDiagnosticsParams, Range,
};
use std::path::PathBuf;

fn run_check(session: &mut Session) -> Result<(), Vec<ErrorComp>> {
    let ast = ast_parse::parse(session)?;
    let _ = hir_lower::check(ast, session)?;
    Ok(())
}

fn url_from_path(path: PathBuf) -> lsp_types::Url {
    match lsp_types::Url::from_file_path(&path) {
        Ok(url) => url,
        Err(()) => panic!("failed to convert `{}` to url", path.to_string_lossy()),
    }
}

fn run_diagnostics() -> Vec<PublishDiagnosticsParams> {
    use std::collections::HashMap;

    //@session errors ignored, its not a correct way to have context in ls server
    // this is a temporary full compilation run
    let mut session = Session::new()
        .map_err(|_| Result::<(), ()>::Err(()))
        .unwrap();
    let errors = if let Err(errors) = run_check(&mut session) {
        errors
    } else {
        vec![]
    };

    // assign empty diagnostics
    let mut diagnostics_map = HashMap::new();
    for file_id in session.file_ids() {
        let path = session.file(file_id).path.clone();
        diagnostics_map.insert(path, Vec::new());
    }

    // generate diagnostics
    for error in errors {
        let (main_message, main_seveiry) = error.main_message();
        let mut diagnostic = Diagnostic::new_simple(Range::default(), "DEFAULT MESSAGE".into());
        let mut main_path = PathBuf::from("");
        let mut related_info = Vec::new();

        for context in error.context_iter() {
            let source = context.source();
            let file = session.file(source.file_id());

            let (start_location, _) =
                text::find_text_location(&file.source, source.range().start(), &file.line_ranges);
            let (end_location, _) =
                text::find_text_location(&file.source, source.range().end(), &file.line_ranges);
            let range = Range::new(
                Position::new(start_location.line() - 1, start_location.col() - 1),
                Position::new(end_location.line() - 1, end_location.col() - 1),
            );

            if context.severity() == main_seveiry {
                main_path = file.path.clone();
                diagnostic = Diagnostic::new_simple(range, main_message.to_string());
                diagnostic.severity = match context.severity() {
                    ErrorSeverity::Error => Some(DiagnosticSeverity::ERROR),
                    ErrorSeverity::Warning => Some(DiagnosticSeverity::WARNING),
                    ErrorSeverity::InfoHint => Some(DiagnosticSeverity::HINT),
                };
            } else {
                related_info.push(DiagnosticRelatedInformation {
                    location: Location::new(url_from_path(file.path.clone()), range),
                    message: context.message().into(),
                });
            }
        }

        diagnostic.related_information = Some(related_info);
        match diagnostics_map.get_mut(&main_path) {
            Some(diagnostics) => {
                diagnostics.push(diagnostic);
            }
            None => {
                diagnostics_map.insert(main_path, vec![diagnostic]);
            }
        }
    }

    //@not using any document versioning
    diagnostics_map
        .into_iter()
        .map(|(path, diagnostics)| {
            PublishDiagnosticsParams::new(url_from_path(path), diagnostics, None)
        })
        .collect()
}
