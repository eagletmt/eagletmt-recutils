const EPS: i64 = 1 * 1000 * 1000; // 1 second

#[derive(serde::Deserialize)]
pub struct Config {
    pub encoder: EncoderConfig,
    pub redis: RedisConfig,
    pub sqs: SqsConfig,
}

#[derive(serde::Deserialize)]
pub struct EncoderConfig {
    pub base_dir: String,
    pub ffmpeg_args: Vec<String>,
}

#[derive(serde::Deserialize)]
pub struct RedisConfig {
    pub url: String,
}

#[derive(serde::Deserialize)]
pub struct SqsConfig {
    pub queue_url: String,
}

pub fn load_config() -> Result<Config, anyhow::Error> {
    let body = std::fs::read("config.toml")?;
    Ok(toml::from_slice(&body)?)
}

pub async fn encode<P>(config: &Config, ts_path: P) -> Result<(), anyhow::Error>
where
    P: AsRef<std::path::Path>,
{
    let ts_path = ts_path.as_ref();
    let mp4_path = ts_path.with_extension("mp4");
    let ts_duration_micro = ffmpeg::format::input(&ts_path)?.duration();

    let status = tokio::process::Command::new("ffmpeg")
        .arg("-i")
        .arg(&ts_path)
        .args(&config.encoder.ffmpeg_args)
        .arg(&mp4_path)
        .status()
        .await?;
    if !status.success() {
        anyhow::anyhow!("Encode failure!");
    }

    let mp4_duration_micro = ffmpeg::format::input(&ts_path)?.duration();
    if (ts_duration_micro - mp4_duration_micro).abs() > EPS {
        anyhow::anyhow!(
            "Duration mismatch: TS {}, MP4 {} (microsecond)",
            ts_duration_micro,
            mp4_duration_micro
        );
    }
    verify_audio_and_video(&mp4_path)?;

    let ts_fname = ts_path.file_name().unwrap().to_str().unwrap();
    let orig_fname = regex::Regex::new(r#"\A\d+_\d+"#)?
        .find(ts_fname)
        .expect("Unexpected filename")
        .as_str();
    let orig_path = ts_path
        .parent()
        .unwrap()
        .join(orig_fname)
        .with_extension("ts");

    std::fs::remove_file(ts_path)?;
    std::fs::remove_file(orig_path)?;
    Ok(())
}

fn verify_audio_and_video<P>(mp4_path: P) -> Result<(), anyhow::Error>
where
    P: AsRef<std::path::Path>,
{
    let audio_path = tempfile::NamedTempFile::new()?.into_temp_path();
    let status = std::process::Command::new("ffmpeg")
        .args(&["-y", "-i"])
        .arg(mp4_path.as_ref())
        .args(&["-vn", "-acodec", "copy", "-f", "mp4"])
        .arg(&audio_path)
        .status()?;
    if !status.success() {
        anyhow::anyhow!("ffmpeg -vn failed");
    }

    let video_path = tempfile::NamedTempFile::new()?.into_temp_path();
    let status = std::process::Command::new("ffmpeg")
        .args(&["-y", "-i"])
        .arg(mp4_path.as_ref())
        .args(&["-an", "-vcodec", "copy", "-f", "mp4"])
        .arg(&video_path)
        .status()?;
    if !status.success() {
        anyhow::anyhow!("ffmpeg -an failed");
    }

    let audio_duration_micro = ffmpeg::format::input(&audio_path)?.duration();
    let video_duration_micro = ffmpeg::format::input(&video_path)?.duration();
    if (audio_duration_micro - video_duration_micro).abs() > EPS {
        anyhow::anyhow!(
            "Duration mismatch! audio:{} video:{} (microsecond)",
            audio_duration_micro,
            video_duration_micro
        );
    }
    Ok(())
}
