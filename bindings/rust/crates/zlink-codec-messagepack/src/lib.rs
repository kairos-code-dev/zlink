use std::fmt;

#[derive(Debug)]
pub enum Error {
    Decode(rmp_serde::decode::Error),
    Encode(rmp_serde::encode::Error),
    Message(zlink::ConfigError),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Decode(err) => write!(f, "messagepack decode failed: {err}"),
            Self::Encode(err) => write!(f, "messagepack encode failed: {err}"),
            Self::Message(err) => write!(f, "zlink message creation failed: {err}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Decode(err) => Some(err),
            Self::Encode(err) => Some(err),
            Self::Message(err) => Some(err),
        }
    }
}

impl From<rmp_serde::decode::Error> for Error {
    fn from(value: rmp_serde::decode::Error) -> Self {
        Self::Decode(value)
    }
}

impl From<rmp_serde::encode::Error> for Error {
    fn from(value: rmp_serde::encode::Error) -> Self {
        Self::Encode(value)
    }
}

impl From<zlink::ConfigError> for Error {
    fn from(value: zlink::ConfigError) -> Self {
        Self::Message(value)
    }
}

pub fn decode<T: serde::de::DeserializeOwned>(msg: &zlink::Message) -> Result<T, Error> {
    rmp_serde::from_slice(msg.as_bytes()).map_err(Error::from)
}

pub fn encode<T: serde::Serialize>(v: &T) -> Result<zlink::Message, Error> {
    let bytes = rmp_serde::to_vec(v)?;
    zlink::Message::try_from(&bytes).map_err(Error::from)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde::{Deserialize, Serialize};

    #[derive(Debug, PartialEq, Serialize, Deserialize)]
    struct Person {
        name: String,
        age: u32,
    }

    #[test]
    fn roundtrip() {
        let original = Person {
            name: "Alice".into(),
            age: 30,
        };
        let msg = encode(&original).expect("encode");
        let got: Person = decode(&msg).expect("decode");
        assert_eq!(original, got);
        drop(msg);
    }
}
