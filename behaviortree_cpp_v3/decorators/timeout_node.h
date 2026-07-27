#ifndef DECORATOR_TIMEOUT_NODE_H
#define DECORATOR_TIMEOUT_NODE_H

// DecoratorNode 관련 정의를 가져온다.
// TimeoutNode는 자식 노드 하나를 감싸서 실행 시간을 제한하는 데코레이터 노드이므로
// DecoratorNode를 상속받기 위해 이 헤더가 필요하다.
#include "../decorator_node.h"

// 여러 스레드나 타이머 콜백에서 같은 값을 안전하게 다루기 위해 사용한다.
// child_halted_ 같은 상태값을 안전하게 변경할 때 필요하다.
#include <atomic>

// 일정 시간이 지난 뒤 특정 작업을 실행하기 위한 타이머 큐이다.
// TimeoutNode는 이 타이머를 이용해서 제한 시간이 지났는지 확인한다.
#include "timer_queue.h"

namespace BT
{

/**
 * TimeoutNode
 *
 * 자식 노드가 지정된 시간보다 오래 RUNNING 상태로 있으면,
 * 자식 노드를 halt() 시키고 FAILURE를 반환하는 데코레이터 노드이다.
 *
 * 사용 목적:
 * - 어떤 행동이 너무 오래 걸릴 때 강제로 중단시키기 위해 사용한다.
 * - 무한 대기, 오래 걸리는 행동, 실패 가능성이 있는 행동에 시간 제한을 걸 수 있다.
 *
 * 동작 방식:
 * - 자식 노드를 실행한다.
 * - 자식 노드가 제한 시간 안에 SUCCESS 또는 FAILURE를 반환하면 그 결과를 처리한다.
 * - 자식 노드가 제한 시간을 넘어서도 RUNNING 상태이면 자식 노드를 halt() 한다.
 * - timeout이 발생하면 TimeoutNode는 FAILURE를 반환한다.
 *
 * 사용 예시:
 *
 * <Timeout msec="5000">
 *    <KeepYourBreath/>
 * </Timeout>
 *
 * 위 예시는 KeepYourBreath 노드가 5000ms, 즉 5초 이상 RUNNING이면
 * 해당 자식 노드를 중단하고 FAILURE를 반환한다.
 */
class TimeoutNode : public DecoratorNode
{
  public:
    /**
     * 생성자 1
     *
     * name:
     * - Behavior Tree 안에서 이 노드를 구분하기 위한 이름이다.
     *
     * milliseconds:
     * - 자식 노드가 RUNNING 상태로 유지될 수 있는 최대 시간이다.
     * - 단위는 millisecond, 즉 ms이다.
     *
     * 예:
     * - milliseconds = 5000이면 자식 노드는 최대 5초까지만 실행될 수 있다.
     */
    TimeoutNode(const std::string& name, unsigned milliseconds);

    /**
     * 생성자 2
     *
     * name:
     * - Behavior Tree 안에서 이 노드를 구분하기 위한 이름이다.
     *
     * config:
     * - XML 포트 정보와 Blackboard 연결 정보를 담고 있는 설정 객체이다.
     *
     * 이 생성자는 제한 시간을 코드에서 직접 받지 않고,
     * XML의 msec 포트를 통해 받을 때 사용한다.
     */
    TimeoutNode(const std::string& name, const NodeConfiguration& config);

    /**
     * 소멸자
     *
     * TimeoutNode가 사라질 때 등록된 타이머를 모두 취소한다.
     *
     * 필요한 이유:
     * - 노드가 삭제된 뒤에도 타이머가 남아 있으면
     *   이미 사라진 객체에 접근하는 문제가 생길 수 있다.
     */
    ~TimeoutNode() override
    {
        // 등록된 모든 타이머 작업을 취소한다.
        timer_.cancelAll();
    }

    /**
     * providedPorts()
     *
     * TimeoutNode가 XML에서 사용할 수 있는 입력 포트를 정의한다.
     *
     * msec:
     * - 제한 시간을 millisecond 단위로 입력받는 포트이다.
     * - 이 시간이 지나도 자식 노드가 RUNNING이면 halt() 된다.
     *
     * 사용 예시:
     * <Timeout msec="5000">
     *     ...
     * </Timeout>
     */
    static PortsList providedPorts()
    {
        return {
            InputPort<unsigned>(
                "msec",
                "After a certain amount of time, "
                "halt() the child if it is still running."
            )
        };
    }

    /**
     * halt()
     *
     * 상위 노드가 TimeoutNode를 중단시킬 때 호출된다.
     *
     * 사용 목적:
     * - 실행 중인 자식 노드를 중지한다.
     * - 아직 남아 있는 타이머를 취소하고 timeout 상태를 초기화한다.
     *
     * 필요한 이유:
     * - 초기화하지 않으면 다음에 다시 실행될 때 이전 타이머가 그대로 남아
     *   시작하자마자 timeout으로 처리되는 문제가 생길 수 있다.
     */
    void halt() override;

  private:
    /**
     * tick()
     *
     * Behavior Tree가 TimeoutNode를 실행할 때 호출되는 핵심 함수이다.
     *
     * 동작 순서:
     * 1. 필요한 경우 msec 포트에서 제한 시간을 읽는다.
     * 2. 자식 노드를 실행한다.
     * 3. 자식이 RUNNING이면 타이머를 시작한다.
     * 4. 제한 시간이 지나면 자식 노드를 halt() 한다.
     * 5. timeout이 발생하면 FAILURE를 반환한다.
     * 6. 자식이 제한 시간 안에 끝나면 자식의 상태에 따라 결과를 반환한다.
     */
    virtual BT::NodeStatus tick() override;

    /**
     * child_halted_
     *
     * 자식 노드가 timeout에 의해 중단되었는지 저장하는 변수이다.
     *
     * atomic을 사용하는 이유:
     * - tick() 실행 흐름과 타이머 콜백이 서로 다른 흐름에서 접근할 수 있기 때문이다.
     * - 여러 곳에서 동시에 값을 바꿔도 안전하게 처리하기 위해 사용한다.
     */
    std::atomic<bool> child_halted_;

    /**
     * timer_id_
     *
     * 현재 등록된 타이머의 ID를 저장한다.
     *
     * 필요한 이유:
     * - 특정 타이머를 취소하거나 관리하기 위해 사용한다.
     * - 새로운 timeout을 시작하기 전에 이전 타이머를 구분할 수 있다.
     */
    uint64_t timer_id_;

    /**
     * msec_
     *
     * 자식 노드가 RUNNING 상태로 유지될 수 있는 제한 시간이다.
     *
     * 단위:
     * - millisecond(ms)
     *
     * 예:
     * - msec_ = 5000이면 최대 5초 동안만 실행을 허용한다.
     */
    unsigned msec_;

    /**
     * read_parameter_from_ports_
     *
     * 제한 시간 msec 값을 포트에서 읽어야 하는지 판단하는 변수이다.
     *
     * true:
     * - XML 또는 Blackboard 포트에서 msec 값을 읽는다.
     *
     * false:
     * - 생성자에서 직접 받은 milliseconds 값을 사용한다.
     */
    bool read_parameter_from_ports_;

    /**
     * timeout_started_
     *
     * timeout 타이머가 이미 시작되었는지 저장하는 변수이다.
     *
     * 필요한 이유:
     * - 자식 노드가 RUNNING 상태일 때 매 tick마다 타이머를 새로 만들면 안 된다.
     * - 타이머가 이미 시작되었는지 확인해서 중복 실행을 막는다.
     */
    bool timeout_started_;

    /**
     * timeout_mutex_
     *
     * timeout 관련 상태를 안전하게 보호하기 위한 mutex이다.
     *
     * 필요한 이유:
     * - 타이머 콜백과 tick()이 같은 상태값에 접근할 수 있다.
     * - 동시에 접근하면서 값이 꼬이는 문제를 막기 위해 사용한다.
     *
     * 참고:
     * - std::mutex를 사용하므로, 컴파일 환경에 따라 <mutex> include가 필요할 수 있다.
     */
    std::mutex timeout_mutex_;

    /**
     * timer_
     *
     * 제한 시간을 관리하기 위한 타이머 큐이다.
     *
     * 필요한 이유:
     * - 자식 노드가 RUNNING 상태가 되었을 때 타이머를 시작한다.
     * - 시간이 초과되면 자식 노드를 중단시키는 작업을 수행한다.
     *
     * 멤버 선언 순서 주의:
     * - 멤버는 선언의 역순으로 소멸되므로, timer_를 마지막에 선언해야
     *   소멸 시 타이머 스레드가 먼저 정리된다.
     * - 그렇지 않으면 이미 소멸된 timeout_mutex_를 타이머 콜백이 잠그려다
     *   프로그램이 죽을 수 있다.
     */
    TimerQueue timer_;
};

} // namespace BT

#endif   // DECORATOR_TIMEOUT_NODE_H