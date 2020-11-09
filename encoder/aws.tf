provider "aws" {
  region  = "ap-northeast-1"
}

terraform {
  backend "s3" {
    bucket = "terraform-wanko-cc"
    key    = "eagletmt-recutils.tfstate"
    region = "ap-northeast-1"
  }
}
