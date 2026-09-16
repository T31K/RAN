

CREATE PROCEDURE [dbo].[UpdateUserMoney]    
    @nUserNum int,
    @llMoney money
AS		
	SET NOCOUNT ON
	
	UPDATE UserInven
	SET UserMoney=@llMoney
	WHERE UserNum=@nUserNum

	SET NOCOUNT OFF


