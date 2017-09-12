<?php

/***********************************************
  Centrebet parser
  php.exe cen.php topicvariant=prd
  php.exe cen.php topicvariant=prd --fastrestart

  Publishes to topics:
    cen.web.cen.gpaunz.<topicvariant>
    cen.web.cen.hnaunz.<topicvariant>
    cen.web.cen.ghaunz.<topicvariant>
    cen.web.cen.gpoth.<topicvariant>
    cen.web.cen.hnoth.<topicvariant>
    cen.web.cen.ghoth.<topicvariant>
************************************************/

error_reporting(E_ALL);

require_once "vendor/autoload.php";
require_once "SynapseClient.php";
require_once "../../../../../federated_serialisation/trunk/utils/utils.php";
require_once "HTTPClient.php";

use Icicle\Loop;
use Icicle\Coroutine\Coroutine;
use Icicle\Promise;
use contests4\SportEnum as SportEnum;
use DataProcessors\HTTP;
use DataProcessors\Synapse;
use DataProcessors\Synapse\SynapseClient;
use DataProcessors\FederatedSerialisation\Utils;

Synapse\register_schemas(["contests4"=>"../../../../../federated_serialisation/trunk/contesthub/gen-php", "DataProcessors"=>"../../../../../federated_serialisation/trunk/thrift/gen-php"]);

class ParserHTTP {
  protected $http;

  public function __construct() {
    global $httpNum;
    $httpNum++;
    $this->httpNum = $httpNum;
    $this->http = new HTTP\HTTPClient();
  }

  public function __destruct() {
    $this->http = null;
  }

  public function Get(string $url, array $headers) {
    $defaultHeaders = array(
        'User-Agent'=>'Mozilla/5.0 (Windows NT 6.1; WOW64; rv:40.0) Gecko/20100101 Firefox/40.0',
        'Accept'=>'*/*',
        'Accept-Language'=>'en-US,en;q=0.5',
        'Accept-Encoding'=>'gzip, deflate',
        'Connection'=>'keep-alive');
    $finalHeaders = array_merge($defaultHeaders, $headers);
    yield $this->http->request('GET', $url, $finalHeaders, null, ['cookiejar'=>'cookies.txt']);//, ['socks_host'=>'127.0.0.1','socks_port'=>9001]);
    yield ['header'=>$this->http->getResponseHeader(), 'content'=>$this->http->getResponseBody()];
  }
}

class CentrebetParser {

  protected $client;
  protected $contests;
  protected $waypoints;
  protected $waypoint;
  public $getOddsJobActive=0;
  public $contestSummary = [];
  public $topicVariant;

  public function __construct(string $topicVariant) {
    $this->client = new SynapseClient('DataProcessors\waypoints');
    $this->topicVariant = $topicVariant;
    $this->contests['cen.web.cen.gpaunz.' . $this->topicVariant] = new contests4\contestsType();
    $this->contests['cen.web.cen.hnaunz.' . $this->topicVariant] = new contests4\contestsType();
    $this->contests['cen.web.cen.ghaunz.' . $this->topicVariant] = new contests4\contestsType();
    $this->contests['cen.web.cen.gpoth.' . $this->topicVariant] = new contests4\contestsType();
    $this->contests['cen.web.cen.hnoth.' . $this->topicVariant] = new contests4\contestsType();
    $this->contests['cen.web.cen.ghoth.' . $this->topicVariant] = new contests4\contestsType();
  }

  public function open() {
    yield $this->client->open('127.0.0.1', 5672, 'guest', 'guest');
    foreach ($this->contests as $topicName=>$contests) {
      yield $this->client->preAllocatePublishingChannel($topicName);
    }
  }

  protected function sleepRandom($minSleep = 2, $maxSleep = 3) {
    yield Promise\resolve()->delay(rand($minSleep, $maxSleep));
  }

  function AddMarkets(contests4\ContestType $contest) {
    //////// add selections for this market ///////
    $participants = $contest->get_participants();
    if ($participants) {
      //echo "populate markets\n";
      foreach ($participants as $participantxid=>$participant) {
        if (isset($participant->userdata)) {
          foreach ($participant->userdata as $raw_key => $betInfo) {
            $betInfo = unserialize($betInfo);
            if (strpos($raw_key,'betinfo_') === 0) {
              unset($price);
              if ($betInfo['HasPrice'] == '1' && isset($betInfo['Content'])) {
                $price = $betInfo['Content'];
                $price = str_replace('*','',$price); // remove asterisk
              }
              $marketKind = $betInfo['BetType'];
              $marketXId = $marketKind;
              $selxid = $participantxid;
              $skip = false;
              if ($marketKind == '15') {
                $market = $contest->set_markets_element($marketXId);
                $market->set_name('Win Fixed Price');
                $market->set_type(1);
                $market->set_simpleSelectionToParticipantMapping(true);
              }
              else
              if ($marketKind == '1500') {
                $market = $contest->set_markets_element($marketXId);
                $market->set_name('Place Fixed Price');
                $market->set_type(2);
                $market->set_simpleSelectionToParticipantMapping(true);
              }
              else {
                $skip = true;
              }

              if (!$skip) {
                $selections = $market->set_selectionsByLeg_element(1);
                $selection = $selections->set_selections_element($selxid);
                $sel_name = null;
                $entities = $participant->get_entities();
                if ($entities) {
                  $entity = $entities->get_horse_element('horse');
                  if ($entity)
                    $sel_name = $entity->get_name();
                  else {
                    $entity = $entities->get_dog_element('dog');
                    if ($entity)
                      $sel_name = $entity->get_name();
                  }
                }

                $selection->set_name($sel_name);
                $selection->set_number($participant->get_number());

                if (isset($price)) {
                  $market->set_availableOffers_element($selxid)
                    ->set_offers_element('p')->set_price($price);
                }
              }
            }
          }
        }
      }
    }
  }

  public function getRace(ParserHTTP $http, string $topicName, string $eventId, bool $full, int $sportcode, string $competition) {
    $contest = $this->contests[$topicName]->set_contest_element($eventId);
    $contest->set_sportCode($sportcode);
    $contest->set_competition($competition);
    if ($full) {
      $url = 'http://centrebet.com/Racing/' . $eventId .'?' . time(). sprintf('%03d', rand(0,999));
      $headers = ['Referer'=>'http://centrebet.com','X-Requested-With'=>'XMLHttpRequest'];  // 'connecttimeout'=>5
      $filename = 'tmpdata/' . $eventId . '_full.html';
      if (!file_exists($filename)) {
        echo "get $url\n";
        $info1 = (yield $http->Get($url,$headers));
        file_put_contents($filename,$info1['header'] . $info1['content']);
        //$this->sleepRandom(1,2);
      }
      else {
        $url = 'http://centrebettest/centrebet.php?filename=' . $filename;
        $info1 = (yield $http->Get($url,$headers));
        //$info1['content'] = file_get_contents($filename);
      }

      /*<div class="venue left">
      <span class="txtWht15">THURLES </span></div>
      */
      if (preg_match('/<div class="venue left">\s*<span class="txtWht15">([^<]*)<\/span>/', $info1['content'], $trackNameMatch)) {
        $contest->set_location()->set_name(html_entity_decode(trim($trackNameMatch[1]),ENT_QUOTES));
      }
      /*<div class="left padLeft10">
            RACE 1
        </div>*/
      if (preg_match('/<div class="left padLeft10">\s*RACE (\d+)\s*<\/div>/s', $info1['content'], $raceNumberMatches)) {
        $contest->set_contestNumber(trim($raceNumberMatches[1]));
      }
      else //Cannot match contest number, do not populate
        return;
      /*<div class="racebar">
            <span class="txtWht11 txtBld padLeft10 lh20 left">[16:10] Templemore...e (5YO plus) 3620m</span>
      */
      if (preg_match('/<div class="racebar">\s*<span[^>]*>(.*?)<\/span>/s', $info1['content'], $raceNameMatches))
        $contest->set_contestName()->set_value(html_entity_decode(trim($raceNameMatches[1]),ENT_QUOTES));

/*
    <div id="divTimeZoneContent_4392246" style="display: none;">

<div class="">04/04/2013 11:30 AM <br/> (AUS Central Standard Time)</div>
        </div>
*/

      if (strpos($info1['content'],'ico-interim-final') !== FALSE) {
        $contest->set_playStatus()->set_value(PlayStatusEnum::PlayStatusEnum_Final);// $raceInfo['playStatus'] = 'Final';
      }

      if (preg_match('/<div id="divTimeZoneContent[^>]+>\s*<div[^>]*>(.*?)<br/s', $info1['content'], $matchStartTime)) {
        // 05/04/2013 12:40 AM
        $ts = trim($matchStartTime[1]);
        $startdatetime = DateTime::createFromFormat('d/m/Y h:i A', $ts);
        $contest->set_startDate()->set_value(utils\EncodeDateFromDateTime($startdatetime));
        $contest->set_startTime()->set_value(utils\EncodeTimeFromDateTime($startdatetime));
      }
      else if (!$contest->get_playStatus()) {
        return;
      }

      /*
        <div class="width100 marLeft3 left txtscratch">
                        9






                    </div>
                        <div class="marLeft3 left martop5 uline pointer txtscratch" onclick="javascript:showForm(4390670, 9);">
                            0857
                        </div>                </li>
                <div class="width94 left height20">


<ul class="width28 left">
    <li class="left txtscratch"><span class=" padLeft10 ">Ajexia</span>
    */

      if (preg_match_all('/<div class="width100 marLeft3([^"]*)">\s*(.*?)<\/div>.*?<span class=" padLeft10 ">(.*?)<\/span>/s', $info1['content'], $horseMatches, PREG_SET_ORDER)) {
        foreach ($horseMatches as $horseMatch) {
          $number = trim($horseMatch[2]);
          $name = html_entity_decode(trim($horseMatch[3]),ENT_QUOTES);
          $name = str_replace('?',"'", $name);
          $name = preg_replace('/\(\dth *e\)/i','',$name);
          $name = str_replace('(3rde)','',$name);
          $name = str_replace('(2nde)','',$name);
          $name = trim(str_replace('(1ste)','',$name));
          $horsexid = $number;
          $name = str_replace('Box Vacant','',$name);
          if (empty($name)) {
            continue;
          }

          $participant = $contest->set_participants_element($horsexid);
          $participant->set_number($number);
          if (strpos($horseMatch[1], 'txtscratch') !== false)
            $participant->set_scratched()->set_value(true);

          $entities = $participant->set_entities();
          if ($sportcode == SportEnum::SportEnum_gh)
          	$entity = $entities->set_dog_element('dog');
          else
          	$entity = $entities->set_horse_element('horse');
          $entity->set_name($name);
        }
      }
      else if (!$contest->get_playStatus()) {
        return;
      }

/*
       <div class="width100 marLeft3 left ">
                        3






                    </div>
                        <div class="marLeft3 left martop5 uline pointer " onclick="javascript:showForm(4392246, 3);">
                            *
                        </div>                </li>
                <div class="width94 left height20">


<ul class="width28 left">
    <li class="left txtWht10"><span class=" padLeft10 ">Ebden</span>
        (4)</li>
*/
/*
<div class="width100 marLeft3 left txtscratch">
                        2






                    </div>
                        <div class="marLeft3 left martop5 uline pointer txtscratch" onclick="javascript:showForm(4392246, 2);">
                            *
                        </div>                </li>
                <div class="width94 left height20">


<ul class="width28 left">
    <li class="left txtscratch"><span class=" padLeft10 ">Camarada</span>
        (2)</li>
*/

    }

    $url = 'http://centrebet.com/RacingRefresh/' . $eventId .'?' . time(). sprintf('%03d', rand(0,999));

    $headers = array(
      'Referer'=>'http://centrebet.com',
      'X-Requested-With'=>'XMLHttpRequest',
      'Accept'=>'application/json, text/javascript, */*; q=0.01',
      'Content-Type'=>'application/json; charset=utf-8');
    $filename = 'tmpdata/' . $eventId.'_refresh.html';
    if (!file_exists($filename)) {
      echo "get $url\n";
      $info2 = (yield $http->Get($url,$headers));
      file_put_contents('tmpdata/' . $eventId . '_refresh.html',$info2['content']);
    }
    else {
      $url = 'http://centrebettest/centrebet.php?filename=' . $filename;
      $info2 = (yield $http->Get($url,$headers));
      //$info2['content'] = file_get_contents($filename);
    }

    $json = json_decode($info2['content'], true);
    //file_put_contents($eventId . '.json',print_r($json,true));

    try {
      // Sometimes CountDown contains a full timestamp (eg Wednesday, 3 April 2013 11:30:00 AM)
      // Othertimes it contains a countdown (eg 2h 29m) which will raise an exception
      $startdatetime = new DateTime($json['CountDown']);
      $contest->set_startDate()->set_value(utils\EncodeDateFromDateTime($startdatetime));
      $contest->set_startTime()->set_value(utils\EncodeTimeFromDateTime($startdatetime));
    } catch (Exception $e) {
      // echo "Exception parsing CountDown: " . $e->getMessage() . "\n";
    }

    if (isSet($json['Competitors'])) {
      foreach ($json['Competitors'] as $competitor) {
        $number = $competitor['CompetitorId'];
        $horsexid = $number;
        $participant = $contest->set_participants_element($horsexid);
        $participant->set_number($number);
        if ($competitor['Scratched'] == '1')
          $participant->set_scratched()->set_value(true);
        foreach ($competitor as $elem_name => $elem_details) {
          if ((strpos($elem_name, 'BetLink') !== false) && isset($elem_details['BetType'])) {
            $participant->userdata['betinfo_' . $elem_name] = serialize($elem_details);
          }
        }
      }
    }

    // Pre-populate some markets that may not exist until close to race time
    //TODO $this->AddFixedMarket('15', $contest); // Win Fixed Price
    //TODO $this->AddFixedMarket('1500', $contest); // Place Fixed Price
    $this->AddMarkets($contest);
  }

  public function getContests(bool $fastRestart = false) {
    echo "Running getContests\n";
    $http = new ParserHTTP();
    if ($fastRestart) {
      $this->contestSummary = unserialize(file_get_contents('contest_summary.txt'));
      echo "Fast restart mode loaded " . count($this->contestSummary) . " contests from contest_summary.txt\n";
      // clean up $contestSummary here to remove any old contests (eg more than 1 hour past sched start time)
    }
    else {
      $sportcodes = [SportEnum::SportEnum_gp,SportEnum::SportEnum_hn,SportEnum::SportEnum_gh];
      foreach ($sportcodes as $sportcode) {
        if (($sportcode == SportEnum::SportEnum_gp) || ($sportcode == SportEnum::SportEnum_hn) || ($sportcode == SportEnum::SportEnum_gh)) {
          echo "sportcode=$sportcode\n";
          for ($dayNumber=0; $dayNumber<=1; $dayNumber++) {
            if ($sportcode == SportEnum::SportEnum_gp)
              $indexUrl = 'http://centrebet.com/Racing/Thoroughbred/' . $dayNumber . '?' . time() . sprintf('%03d', rand(0,999));
            else
            if ($sportcode == SportEnum::SportEnum_gh)
              $indexUrl = 'http://centrebet.com/Racing/Greyhound/' . $dayNumber . '?' . time() . sprintf('%03d', rand(0,999));
            else
            if ($sportcode == SportEnum::SportEnum_hn)
              $indexUrl = 'http://centrebet.com/Racing/Harness/' . $dayNumber . '?' . time() . sprintf('%03d', rand(0,999));

            $headers = ['Referer'=>'http://centrebet.com','X-Requested-With'=>'XMLHttpRequest'];

            $filename = 'tmpdata/races' . $sportcode . '_' . $dayNumber . '.txt';
            if (!file_exists($filename)) {
              $info = (yield $http->Get($indexUrl, $headers));
              echo "Got $indexUrl body=" . strlen($info['content']) . " bytes\n";
              file_put_contents($filename,$info['header'] . $info['content']);
              //yield $this->sleepRandom();
            }
            else {
              $url = 'http://centrebettest/centrebet.php?filename=' . $filename;
              $info = (yield $http->Get($url,$headers));
            }

            $multiplesPos = strpos($info['content'],'>- Multiples<');
            if ($multiplesPos !== false)
              $raceSection = substr($info['content'],0,$multiplesPos);
            else
              $raceSection = $info['content'];

            preg_match_all('/<div class="racecard-venue">.*?(?=<div class="racecard-venue">|$)/s', $raceSection, $matchByVenue, PREG_SET_ORDER);
            foreach ($matchByVenue as $venueRaces) {
              if (preg_match('/<div class="ico-flag">\s*<div class="ico-(\d+)">/s', $venueRaces[0], $matchFlag)) {
                if ($matchFlag[1] == 13)
                  $competition = 'Australian Racing';
                else
                if ($matchFlag[1] == 159)
                  $competition = 'New Zealand Racing';
                else
                if ($matchFlag[1] == 230)
                  $competition = 'GB Racing';
                else
                if ($matchFlag[1] == 108)
                  $competition = 'Ireland Racing';
                else
                if ($matchFlag[1] == 197)
                  $competition = 'South Africa Racing';
                else
                if ($matchFlag[1] == 101)
                  $competition = 'Hong Kong Racing';
                else
                if ($matchFlag[1] == 192)
                  $competition = 'Singapore Racing';
                else
                if ($matchFlag[1] == 76)
                  $competition = 'France Racing';
                else
                if ($matchFlag[1] == 112)
                  $competition = 'Japan Racing';
                else
                if ($matchFlag[1] == 229)
                  $competition = 'UAE Racing';
                else
                if ($matchFlag[1] == 231)
                  $competition = 'US Racing';
                else
                if ($matchFlag[1] == 210)
                  $competition = 'SWE Racing';
                else
                  $competition = 'Unknown flag ' . $matchFlag[1];
              }
              else
                $competition = '';
              if (preg_match_all('/javascript:link\(\'Racing\/(\d+)\'\)/is', $venueRaces[0], $matches, PREG_SET_ORDER)) {
                foreach ($matches as $match) {
                  $eventId = trim($match[1]);
                  $topicName = $this->getTopicName($sportcode, $competition);
                  if ($topicName) {
                    echo "Getting event sportcode=$sportcode competition=$competition theirId=$eventId\n";
                    yield $this->getRace($http, $topicName, $eventId, true, $sportcode, $competition);
                    $this->sleepRandom(1,2);
                  }
                  else
                    echo "Could not determine topicname for sportcode=$sportcode competition=$competition theirId=$eventId\n";
                }
              }
            }
          }
        }
      }
      echo "Done getContests\n";
      //
      $this->contestSummary = [];
      foreach ($this->contests->get_contest() as $key=>$contest) {
        $info = [];
        if ($contest->get_startDate())
          $info['startDate'] = $contest->get_startDate()->get_value();
        if ($contest->get_startTime())
          $info['startTime'] = $contest->get_startTime()->get_value();
        $info['sportCode'] = $contest->get_sportCode();
        $info['competition'] = $contest->get_competition();
        $this->contestSummary[$key] = $info;
      }
      file_put_contents('contest_summary.txt', serialize($this->contestSummary));  ///// do this with sqlite?  could store lastPoll and lastFull every time there is a change
    }
    //
    foreach ($this->contestSummary as $key=>&$info) {
      $info['lastPoll'] = time() - rand(0,30);
      $info['sentFull'] = false;
      $info['pollInterval'] = 30; // seconds between polls
      if (isset($info['competition']) && isset($info['sportCode']))
        $info['topicName'] = $this->getTopicName($info['sportCode'], $info['competition']);
      else
        unset($this->contestSummary[$key]);
    }
    //
    $this->waypoints = new DataProcessors\waypoints();
    $this->waypoint = new DataProcessors\waypoint();
    $this->waypoint->set_timestamp(utils\EncodeNow());
    $this->waypoint->set_tag("cen.php");
    $this->waypoints->add_path_element($this->waypoint);
    //
    foreach ($this->contests as $topicName=>$contests) {
      $bytes = (yield $this->client->publish($topicName, $contests, $this->waypoints, true));  // even the initial message is sent as delta
      echo "published initial $topicName (as delta) ($bytes payload bytes)\n";
    }
  }

  function getTopicName(int $sportcode, string $competition) {
    $topicName = '';
    if (($competition == 'Australian Racing') || ($competition == 'New Zealand Racing')) {
      if ($sportcode == SportEnum::SportEnum_gp)
        $topicName = 'cen.web.cen.gpaunz.' . $this->topicVariant;
      else
      if ($sportcode == SportEnum::SportEnum_hn)
        $topicName = 'cen.web.cen.hnaunz.' . $this->topicVariant;
      else
      if ($sportcode == SportEnum::SportEnum_gh)
        $topicName = 'cen.web.cen.ghaunz.' . $this->topicVariant;
    }
    else {
      if ($sportcode == SportEnum::SportEnum_gp)
        $topicName = 'cen.web.cen.gpoth.' . $this->topicVariant;
      else
      if ($sportcode == SportEnum::SportEnum_hn)
        $topicName = 'cen.web.cen.hnoth.' . $this->topicVariant;
      else
      if ($sportcode == SportEnum::SportEnum_gh)
        $topicName = 'cen.web.cen.ghoth.' . $this->topicVariant;
    }
    return $topicName;
  }

  function getOdds(string $eventId, string $topicName, bool $full, int $sportcode, string $competition) {
    $this->getOddsJobActive++;
    $nChangedPrices = 0;
    $http = new ParserHTTP();
    try {
      if ($full) {
        yield $this->getRace($http, $topicName, $eventId, true, $sportcode, $competition);
      }
      else {
        yield $this->getRace($http, $topicName, $eventId, false, $sportcode, $competition);
        // simulate only for testing!!  in practice only do a call to getRace from here
        $contest = $this->contests[$topicName]->set_contest_element($eventId);
        $markets = $contest->get_markets();
        foreach ($markets as $market) {
          $availableOffers = $market->get_availableOffers();
          foreach ($availableOffers as $selxid=>$offers) {
            if (rand(0,100)<30) {
              $offer = $offers->get_offers_element('p');
              $price = $offer->get_price();
              $price += (rand(1,100)-50)*0.01;
              $offer->set_price($price);
              $nChangedPrices++;
            }
          }
        }
      }
      //
      $this->waypoint->set_timestamp(utils\EncodeNow());
      global $messageCount;
      $bytes = yield $this->client->publish($topicName, $this->contests[$topicName], $this->waypoints, true);
      //echo "\npublished delta ($bytes payload bytes) getOdds $eventId full=$full nChangedPrices=$nChangedPrices\n";
      $messageCount++;
    }
    finally {
      $this->getOddsJobActive--;
    }
  }

  protected function doPolling() {
    $http = new ParserHTTP();
    $nextFullTime = time() + 300;
    $lastCheckFulls = 0;
    $lastSummaryTime = time();
    while (true) {
      $curTime = time();
      //
      $numFull = 0;
      $dirtyFullTopics = [];
      if ($curTime - $lastCheckFulls > 0) { // check fulls every second
      	$lastCheckFulls = $curTime;
	      if ($curTime >= $nextFullTime) { // we have passed the interval, move to the next
	        $nextFullTime += 300;
	        foreach ($this->contestSummary as $key=>&$info) { // reset the 'sent full' flags
		      $info['sentFull'] = false;
		    }
	    }
	    $timeToNextFull = $nextFullTime - $curTime; // how much time is remaining until the next interval
	    global $countFullsToDo;
	    $countFullsToDo = 0; // how many fulls remain to be sent
	    foreach ($this->contestSummary as $key=>$info) {
	      if (!$info['sentFull'])
	        $countFullsToDo++;
	     }
	     $numFullsPerSec = (int) round($countFullsToDo / $timeToNextFull) + 1;  // rate needed to send remaining fulls by the next interval
      }
      else
      	$numFullsPerSec = 0;
      foreach ($this->contestSummary as $key=>&$info) {
        if ($this->getOddsJobActive < 100) {
          $sinceLastPoll = ($curTime - $info['lastPoll']);
          if ($sinceLastPoll >= $info['pollInterval']) {
            $contest = $this->contests[$info['topicName']]->get_contest_element($key);
            if ($contest === null) { // contest not present. Fetching full info.
              $info['sentFull'] = true;
              $doFull = true;
            }
            else
              $doFull = false;
            $info['lastPoll'] = $curTime;
            new Coroutine($this->getOdds($key, $info['topicName'], $doFull, $info['sportCode'], $info['competition']));
          }
        }

        if ($numFull < $numFullsPerSec) {
	        $contest = $this->contests[$info['topicName']]->get_contest_element($key);
	        if ($contest !== null && !$info['sentFull']) {
            $dirtyFullTopics[$info['topicName']] = true;
	          $info['sentFull'] = true;
	          $numFull++;
	          $contest->inhibit_delta();
	        }
	      }
      }
      //
      if ($numFull > 0) {
        $this->waypoint->set_timestamp(utils\EncodeNow());
        foreach ($dirtyFullTopics as $topicName=>$dummy) {
          $bytes = yield $this->client->publish($topicName, $this->contests[$topicName], $this->waypoints, true);
          global $messageCount;
          $messageCount++;
          //echo "\n[" . date('Y-m-d H:i:s') . "] published numFull=$numFull ($bytes payload bytes) topicName=$topicName\n";
        }
      }
      yield Promise\resolve()->delay(0.1);

      /////////////////
      if ($curTime - $lastSummaryTime > 30) {
        $lastSummaryTime = $curTime;
        echo "\n";
        foreach ($this->contests as $topicName=>$contests) {
          echo $topicName . " " . count($contests->get_contest()) . "\n";
        }
        
        /*echo "Running perf test...\n";
        for ($n=1;$n<1000;$n++) {
          $this->waypoint->set_timestamp(utils\EncodeNow());
          foreach ($this->contests as $topicName=>$contests) {
            $bytes = yield $this->client->publish($topicName, $this->contests[$topicName], $this->waypoints, false);
            echo "\n[" . date('Y-m-d H:i:s') . "] published full ($bytes payload bytes) topicName=$topicName\n";
          }
        }*/
      }
      /////////////////
    }
  }

  public function go(bool $fastRestart = false) {
    yield $this->getContests($fastRestart);
    yield $this->doPolling();
  }
}

function main() {

  $fastRestart = false;
  $topicVariant = '';
  global $argv;
  foreach ($argv as $arg) {
    if ($arg == '--fastrestart') {
      $fastRestart = true;
    }
    else
    if (preg_match('/^topicvariant=(.*)/', $arg, $matchTopic)) {
      $topicVariant = $matchTopic[1];
    }
  }

  if ($topicVariant == '')
    die("topicvariant parameter must be specified on command line");

  $parser = new CentrebetParser($topicVariant);
  yield $parser->open();
  echo "[" . date('Y-m-d H:i:s') . "] Opened SynapseClient.\n";

  $getContests = new Coroutine($parser->go($fastRestart));

  $timer = Loop\periodic(1, function() use ($parser) {
    global $messageCount;
    if ($messageCount > 0) {
      global $st_time;
      if (!isset($st_time)) {
        $st_time = microtime(true);
      }
      $cur_time = microtime(true);
      $elapsed = $cur_time-$st_time;
      if ($elapsed > 0)
        $rate = $messageCount / $elapsed;
      else
        $rate = '';

      $curTime = time();
      $countLatePoll = 0;
      foreach ($parser->contestSummary as $key=>$info) {
        $latePoll = $curTime - $info['lastPoll'] - $info['pollInterval'];
        if ($latePoll > 2) { // deem late if more than 2 seconds past when it should have been polled
          $countLatePoll++;
        }
      }

      global $countFullsToDo;
      echo "\r" . sprintf('%6d', $messageCount) . ' ' . sprintf('%6.2f', $rate) . ' Hz  ' . sprintf('%3d',$parser->getOddsJobActive)  . ' jobs active; ' .  sprintf('%3d', $countLatePoll) .
        ' polls are late; ' .  sprintf('%3d',$countFullsToDo) . ' fulls to do';
    }
  });

}

$main = new Coroutine(main());
Loop\run();
