BRRS Experiment 3 PyLink capture update (2026-08-12)

1. ZIP 안의 파일을 대상 SDK의 Drivers/API에 덮어쓴다.
2. 다음 파일에 실행 권한을 준다.

   chmod +x Drivers/API/brrs_exp3_capture.sh

3. Drivers/API에서 실행한다.

   ./brrs_exp3_capture.sh tx A 1 iron_door_nlos 6.9

기존 brrs_exp3_flash_and_log.sh는 두 J-Link 연결을 사용하므로 더 이상 쓰지 않는다.
자세한 순서는 BRRS_EXPERIMENT3_GUIDE_KR.md를 참고한다.
