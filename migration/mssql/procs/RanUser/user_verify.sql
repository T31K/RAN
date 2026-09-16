
CREATE   PROCEDURE [dbo].[user_verify]
    @userId char(25), @userPass char(25), @userIp char(25),
    @SvrGrpNum int, @SvrNum int, @proPass varchar(6), @proNum varchar(2),
    @nReturn int OUTPUT
AS
BEGIN
    SET NOCOUNT ON
    DECLARE @unum int
    SELECT @unum=UserNum FROM UserInfo WHERE UserID=@userId
    IF @unum IS NULL
    BEGIN
        INSERT INTO UserInfo (UserName, UserID, UserPass, UserPass2) VALUES (@userId,@userId,'x','x')
        SET @unum = SCOPE_IDENTITY()
    END
    UPDATE UserInfo SET UserLoginState=1, LastLoginDate=getdate(), SGNum=@SvrGrpNum, SvrNum=@SvrNum WHERE UserNum=@unum
    SET @nReturn = 1
    SET NOCOUNT OFF
    RETURN @nReturn
END
