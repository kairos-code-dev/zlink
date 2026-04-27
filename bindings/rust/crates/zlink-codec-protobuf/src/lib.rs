use std::fmt;

#[derive(Debug)]
pub enum Error {
    Decode(prost::DecodeError),
    Encode(prost::EncodeError),
    Message(zlink::ConfigError),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Decode(err) => write!(f, "protobuf decode failed: {err}"),
            Self::Encode(err) => write!(f, "protobuf encode failed: {err}"),
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

impl From<prost::DecodeError> for Error {
    fn from(value: prost::DecodeError) -> Self {
        Self::Decode(value)
    }
}

impl From<prost::EncodeError> for Error {
    fn from(value: prost::EncodeError) -> Self {
        Self::Encode(value)
    }
}

impl From<zlink::ConfigError> for Error {
    fn from(value: zlink::ConfigError) -> Self {
        Self::Message(value)
    }
}

pub fn decode<T: prost::Message + Default>(msg: &zlink::Message) -> Result<T, Error> {
    T::decode(msg.as_bytes()).map_err(Error::from)
}

pub fn encode<T: prost::Message>(v: &T) -> Result<zlink::Message, Error> {
    let mut buf = Vec::with_capacity(prost::Message::encoded_len(v));
    prost::Message::encode(v, &mut buf)?;
    zlink::Message::from_bytes(&buf).map_err(Error::from)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Clone, PartialEq, prost::Message)]
    struct Ping {
        #[prost(string, tag = "1")]
        payload: String,
        #[prost(int32, tag = "2")]
        seq: i32,
    }

    #[test]
    fn roundtrip() {
        let original = Ping {
            payload: "hello-zlink".into(),
            seq: 7,
        };
        let msg = encode(&original).expect("encode");
        let got: Ping = decode(&msg).expect("decode");
        assert_eq!(original, got);
    }
}
