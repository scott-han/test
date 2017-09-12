<?php
namespace DataProcessors\HTTP;

require_once "cookie/SetCookie.php";
require_once "cookie/CookieJarInterface.php";
require_once "cookie/CookieJar.php";
require_once "cookie/FileCookieJar.php";

use Icicle\Socket\Client\Connector;
use GuzzleHttp\Cookie;

/*
  HTTP Client

  * Async friendly
  * Supports SSL and persistent connections
  * Supports socks5 proxy
  * Supports cookies (persisted to file)
  * Supports GET, POST
  * Decodes chunked, gzip, inflate
*/

class HTTPClient {

  protected $responseHeader;
  protected $responseBody;
  protected $headerLength;
  protected $bodyLength;
  protected $client = null;
  protected $cookieJar = null;
  protected $cookieJarFilename = null;

  public function __destruct() {
    $this->close();
  }

  public function getResponseHeader() {
    return $this->responseHeader;
  }

  public function getResponseBody() {
    return $this->responseBody;
  }

  public function getResponseHeaderLength() {
    return $this->headerLength;
  }

  public function getResponseBodyLength() {
    return $this->bodyLength;
  }

  public function close() {
    if ($this->client !== null) {
      $this->client->close();
      $this->client = null;
    }
  }

  public function isOpen() {
    if ($this->client !== null)
      return $this->client->isOpen();
    else
      return false;
  }

  protected function buildRequest(string $method, string $path, string $host, string $query, array $headers) {
    if ($query != '')
      $query = '?' . $query;
    $request_msg =
      "$method $path$query HTTP/1.1\r\n" .
      "Host: $host\r\n";
    foreach ($headers as $name => $value) {
      $request_msg .= "$name: $value\r\n";
    }
    $request_msg .=
      "\r\n";

    return $request_msg;
  }

  protected function decodeURI(string $uri) {
    $url_parts = parse_url($uri);

    $url_parts['scheme'] = isset($url_parts['scheme']) ? $url_parts['scheme'] : '';
    $url_parts['host'] = isset($url_parts['host']) ? $url_parts['host'] : '';
    $url_parts['port'] = isset($url_parts['port']) ? $url_parts['port'] : 0;
    $url_parts['path'] = isset($url_parts['path']) ? $url_parts['path'] : '/';
    $url_parts['query'] = isset($url_parts['query']) ? $url_parts['query'] : '';

    if ($url_parts['port'] == 0) {
      if ($url_parts['scheme'] == 'http')
        $url_parts['port'] = 80;
      else
      if ($url_parts['scheme'] == 'https')
        $url_parts['port'] = 443;
    }

    return $url_parts;
  }

  protected function parseCookies(array $rawSetCookies, string $host, string $path) {
    if ($this->cookieJar != null) {
      foreach ($rawSetCookies as $setCookie) {
        $this->cookieJar->extractCookie($setCookie, $host);
      }
    }
  }

  protected function readResponse(array $url_parts) {
    $contentLength = 0;
    $transferEncoding = '';
    $contentEncoding = '';
    $chunkSize = 0;
    $dechunkedBody = '';
    $endOfChunked = false;
    $gotHeader = false;
    $rawSetCookies = [];
    while ($this->client->isReadable()) {
      $r = (yield $this->client->read());
      if (!$gotHeader) {
        if (($pos = strpos($r, "\r\n\r\n")) !== false) {
          $this->responseBody = substr($r, $pos+4);
          $this->bodyLength = strlen($this->responseBody);
          $this->responseHeader .= substr($r, 0, $pos+4);
          $this->headerLength = strlen($this->responseHeader);
          //
          $lines = preg_split('/(\r?\n)/', $this->responseHeader, -1); // be generous with allowable line breaks inside headers section
          foreach ($lines as $line) {
            if (preg_match('/^Set-Cookie: +(.*)/i', $line, $matchCookie)) {
              $rawSetCookies[] = $matchCookie[1];
              //echo "**Set-Cookie: " . $matchCookie[1] . "\n";
            }
            else
            if (preg_match('/^Content-Length: +([0-9]+)/i', $line, $matchContentLength)) {
              $contentLength = $matchContentLength[1];
              //echo "Content-Length: " . $contentLength . "\n";
            }
            else
            if (preg_match('/^Content-Encoding: +(.*)/i', $line, $matchContentEncoding)) {
              $contentEncoding = $matchContentEncoding[1];
              //echo "Content-Encoding: " . $contentEncoding . "\n";
            }
            else
            if (preg_match('/^Transfer-Encoding: +(.*)/i', $line, $matchTransferEncoding)) {
              $transferEncoding = $matchTransferEncoding[1];
              //echo "Transfer-Encoding: " . $transferEncoding . "\n";
            }
          }
          //
          $gotHeader = true;
        }
        else
          $this->responseHeader .= $r;
      }
      else {
        $this->responseBody .= $r;
        $this->bodyLength += strlen($r);
      }

      if ($transferEncoding == 'chunked') {
        while (true) {
          if ($chunkSize == 0) {
            //echo "Decoding chunked...\n";
            if (preg_match('/^([a-fA-F0-9]+).*?\r\n/', $this->responseBody, $matchChunkSize)) {
              $chunkSize = hexdec($matchChunkSize[1]);
              $chunkPtr = strlen($matchChunkSize[0]);
              if ($chunkSize == 0) {
                //echo "[final chunk]\n";
                if (($pos = strpos($this->responseBody,"\r\n\r\n")) !== false) {
                  //echo "[skipped to end of chunk]\n";
                  $this->responseBody = substr($r, $pos+4);
                  $this->bodyLength = strlen($this->responseBody);
                  $endOfChunked = true;
                  break;
                }
              }
            }
          }
          if ($chunkSize > 0) {
            //echo "chunkSize=$chunkSize chunkPtr=$chunkPtr bodyLength=" . $this->bodyLength . "\n";
            if ($this->bodyLength >= $chunkSize + $chunkPtr + 2) {
              //echo "  got a chunk size $chunkSize\n";
              $dechunkedBody .= substr($this->responseBody, $chunkPtr, $chunkSize);
              $this->responseBody = substr($this->responseBody, $chunkSize + $chunkPtr + 2);
              $this->bodyLength = strlen($this->responseBody);
              $chunkSize = 0;
              $chunkPtr = 0;
            }
            else
              break;
          }
          else
            break;
        }
      }

      if ($endOfChunked) {
        $this->responseBody = $dechunkedBody;
        $this->bodyLength = strlen($this->responseBody);
        break;
      }
      else
      if (($contentLength > 0) && ($this->bodyLength >= $contentLength))
        break;
    }
    // Error reporting suppressed since zlib_decode() emits a warning if decompressing fails.
    if (($contentEncoding == 'gzip') || ($contentEncoding == 'deflate')) {
      $this->responseBody = @zlib_decode($this->responseBody);
    }

    $this->parseCookies($rawSetCookies, $url_parts['host'], $url_parts['path']);
  }

  protected function connectSOCKSProxy(array $options, array $url_parts) {
    // For SOCKS5 protocol spec see https://tools.ietf.org/rfc/rfc1928.txt

    $connector = new Connector();

    // First connect to the socks proxy server
    $this->client = (yield $connector->connect($options['socks_host'], $options['socks_port'], ['name'=>$url_parts['host']]));

    // Specify "No Authentication Required" method
    yield $this->client->write(pack("C3", 0x05, 0x01, 0x00));
    $r = (yield $this->client->read(2));
    if ($r == pack( "C2", 0x05, 0x00 )) {
      // SOCKS negotiation succeeded
    }
    else
      throw new Exception('SOCKS negotiation failed');

    // Instruct the socks server to connect to host:port
    yield $this->client->write(pack( "C5", 0x05, 0x01, 0x00, 0x03, strlen( $url_parts['host'] ) ) . $url_parts['host'] . pack( "n", $url_parts['port'] ));

    $r = (yield $this->client->read(10));
    if (strlen($r) != 10)
      throw new Exception('Expecting 10 bytes in SOCKS CONNECT response but received ' . strlen($r));

    if (ord($r[0]) == 5 && ord($r[1]) == 0 && ord($r[2]) == 0 && ord($r[3]) == 1) { // r[3]=1 means IPv4 ... this might break on IPv6; The number of bytes in the reply for IPv6 is 16 instead of 4 bytes for IPv4
      // SOCKS CONNECT succeeded
    }
    else
      throw new Exception("The SOCKS server failed to connect to " . $url_parts['host'] . ":" .  $url_parts['port']);
  }

  /*
    Supported options:
      socks_host => socks proxy host
      socks_port => socks proxy port
      cookiejar => filename to store cookies
  */
  public function request(string $method, string $uri, array $headers = [],
      string $body = null, array $options = []) {
    //
    $this->responseHeader = '';
    $this->responseBody = '';
    $this->headerLength = 0;
    $this->bodyLength = 0;
    //
    $url_parts = $this->decodeURI($uri);

    // if already got an open client/connection then continue to use it (persistent connection)
    if ($this->client === null || !$this->client->isOpen()) {
      if (isset($options['socks_host']))
        yield $this->connectSOCKSProxy($options, $url_parts);
      else {
        $connector = new Connector();
        $this->client = (yield $connector->connect($url_parts['host'], $url_parts['port'], ['name'=>$url_parts['host']]));
      }
      if ($url_parts['scheme'] == 'https')
        yield $this->client->enableCrypto(STREAM_CRYPTO_METHOD_TLS_CLIENT);
    }

    // Cookies: open existing cookie jar and get cookies applicable to this request, or create a new cookie jar
    // Cookie jar file will be saved when it is freed
    if (isset($options['cookiejar'])) {
      if ($this->cookieJarFilename != $options['cookiejar']) {
        $this->cookieJarFilename = $options['cookiejar'];
        $this->cookieJar = new Cookie\FileCookieJar($options['cookiejar'],false);
      }
      $cookie = $this->cookieJar->getCookieHeader($url_parts['scheme'], $url_parts['host'], $url_parts['path']);
      if ($cookie != '')
        $headers['Cookie'] = $cookie;
    }

    $request_msg = $this->buildRequest($method, $url_parts['path'], $url_parts['host'], $url_parts['query'], $headers);

    yield $this->client->write($request_msg);

    if ($body !== null)
      yield $this->client->write($body);

    yield $this->readResponse($url_parts);
  }
}
