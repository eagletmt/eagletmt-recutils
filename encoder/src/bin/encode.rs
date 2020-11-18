#[tokio::main]
async fn main() -> Result<(), anyhow::Error> {
    ffmpeg::init()?;

    let config = encoder::load_config()?;
    let ts_path = std::path::PathBuf::from(std::env::args().nth(1).expect("missing file"));
    encoder::encode(&config, ts_path).await
}
