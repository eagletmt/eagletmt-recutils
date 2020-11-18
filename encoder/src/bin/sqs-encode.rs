#[tokio::main]
async fn main() -> Result<(), anyhow::Error> {
    use anyhow::Context as _;
    use futures::StreamExt as _;
    use rusoto_sqs::Sqs as _;

    let config = encoder::load_config()?;
    let sqs_client = rusoto_sqs::SqsClient::new(Default::default());
    let stop_path = std::path::Path::new("/tmp/stop-encode.txt");
    let base_dir = std::path::Path::new(&config.encoder.base_dir);

    loop {
        if stop_path.exists() {
            break;
        }
        let resp = sqs_client
            .receive_message(rusoto_sqs::ReceiveMessageRequest {
                queue_url: config.sqs.queue_url.clone(),
                wait_time_seconds: Some(5),
                visibility_timeout: Some(60),
                ..Default::default()
            })
            .await
            .context("failed to call sqs:ReceiveMessage")?;
        if let Some(messages) = resp.messages {
            let message = messages.into_iter().next().unwrap();
            let fname = message.body.expect("SQS message body is missing");
            let message_id = message.message_id.expect("SQS message_id is missing");
            let receipt_handle = message
                .receipt_handle
                .expect("SQS receipt_handle is missing");
            println!("[message_id={}] {}", message_id, fname);

            let ts_path = base_dir.join(format!("{}.ts", fname));
            if ts_path.exists() {
                let interval = tokio::time::interval(tokio::time::Duration::from_secs(60))
                    .map(|_| futures::future::Either::Left(()));
                let encode = futures::stream::once(encoder::encode(&config, ts_path))
                    .map(|result| futures::future::Either::Right(result));
                tokio::pin!(encode);
                let mut stream = futures::stream::select(interval, encode);

                while let Some(item) = stream.next().await {
                    match item {
                        futures::future::Either::Left(_) => {
                            let result = sqs_client
                                .change_message_visibility(
                                    rusoto_sqs::ChangeMessageVisibilityRequest {
                                        queue_url: config.sqs.queue_url.clone(),
                                        receipt_handle: receipt_handle.clone(),
                                        visibility_timeout: 70,
                                    },
                                )
                                .await;
                            if let Err(e) = result {
                                eprintln!("Failed to change message visibility: {:?}", e);
                            }
                        }
                        futures::future::Either::Right(result) => {
                            match result {
                                Ok(_) => {
                                    delete_message_with_retry(
                                        &sqs_client,
                                        &config.sqs.queue_url,
                                        &receipt_handle,
                                    )
                                    .await?;
                                }
                                Err(e) => {
                                    eprintln!("encode failed: {:?}", e);
                                }
                            }
                            break;
                        }
                    }
                }
            } else {
                let mp4_path = base_dir.join(format!("{}.mp4", fname));
                if mp4_path.exists() {
                    println!(
                        "{} is already encoded to {}",
                        ts_path.display(),
                        mp4_path.display()
                    );
                    delete_message_with_retry(&sqs_client, &config.sqs.queue_url, &receipt_handle)
                        .await?;
                } else {
                    println!("{} does not exist", ts_path.display());
                }
            }
        } else {
            break;
        }
    }

    Ok(())
}

async fn delete_message_with_retry<Sqs>(
    sqs_client: &Sqs,
    queue_url: &str,
    receipt_handle: &str,
) -> Result<(), anyhow::Error>
where
    Sqs: rusoto_sqs::Sqs,
{
    for i in 0..3 {
        match sqs_client
            .delete_message(rusoto_sqs::DeleteMessageRequest {
                queue_url: queue_url.to_owned(),
                receipt_handle: receipt_handle.to_owned(),
            })
            .await
        {
            Ok(_) => {
                return Ok(());
            }
            Err(e) => {
                eprintln!("[{}] failed to call sqs:DeleteMessage: {}", i, e);
            }
        }
    }
    Err(anyhow::anyhow!("sqs:DeleteMessage failed"))
}
