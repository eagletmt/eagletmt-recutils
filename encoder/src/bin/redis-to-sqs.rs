#[tokio::main]
async fn main() -> Result<(), anyhow::Error> {
    use redis::Commands as _;
    use rusoto_sqs::Sqs as _;

    let config = encoder_reinforce::load_config()?;
    let redis_client = redis::Client::open(config.redis.url)?;
    let mut conn = redis_client.get_connection()?;
    let sqs_client = rusoto_sqs::SqsClient::new(Default::default());

    loop {
        let job: Vec<String> = conn.blpop(&["jobs", "0"], 5)?;
        if job.is_empty() {
            break;
        }
        let fname = job.into_iter().nth(1).unwrap();
        println!("Enqueue {}", fname);

        sqs_client
            .send_message(rusoto_sqs::SendMessageRequest {
                queue_url: config.sqs.queue_url.clone(),
                message_body: fname,
                ..Default::default()
            })
            .await?;
    }
    Ok(())
}
