import termios
from test_idle_terminal import IdleSession
from test_idle_setup import start_log,open_setup,enter_answer,wait_question

QUESTIONS = ['History lines','Send mode','Composer mode','Output format',
             'Queue notices','Program messages','Persistent logging',
             'Single-session mode','Command suggestions','Use signals']

def panel(s,signal_value):
    open_setup(s,'panel')
    enter_answer(s,'',next_question=QUESTIONS[0])
    for n in range(len(QUESTIONS)-1):
        enter_answer(s,'',next_question=QUESTIONS[n+1])
    enter_answer(s,signal_value,next_question='Finish')
    enter_answer(s,'2') # Apply without saving (the test session is offline).
    s.wait(lambda: not s.screen.alternate,'failed to leave application panel')
    assert bool(termios.tcgetattr(s.slave)[3] & termios.ISIG) == (signal_value=='yes')

with IdleSession(no_connect=True,cols=100,review=False) as s:
    start_log(s)
    assert not termios.tcgetattr(s.slave)[3] & termios.ISIG
    panel(s,'yes')
    panel(s,'no')
    s.type('after-panel-draft')
    s.wait(lambda: s.editor('after-panel-draft',17),'draft not visible')
    mark=len(s.raw)
    s.type(b'\x1a')
    s.wait(lambda: b'Draft cancelled' in s.raw[mark:] and not s.screen.alternate,
           'Ctrl-Z failed to cancel after live panel Apply')
    assert s.proc.poll() is None
print('PASS: panel 0 live Apply toggles ISIG both ways; Ctrl-Z cancels after re-disable')
